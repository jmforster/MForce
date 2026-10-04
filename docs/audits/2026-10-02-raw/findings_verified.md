# All merged findings with verification outcome

## F001 [CONFIRMED] low (reporter: medium) performance — CMakeLists.txt:4

**ISA baseline and floating-point model are implicit; hot-loop code is hand-written around an unstated build constraint and correctness depends on the unstated /fp default**

Reporters: tools-misc-build

Evidence: `No '/arch:' and no '/fp:' in any CMakeLists (grep: no hits). engine/include/mforce/source/additive/partials.h:679-681: 'Under the engine's Release flags (/O2, SSE2 baseline, no /arch) MSVC emits a CRT CALL for std::truncf - roundss is SSE4.1 and therefore off the table'; fast_math.h:90 'those are CRT calls under SSE2 baseline'; partials.h:673 'return std::numeric_limits<float>::quiet_NaN()' is a NaN-as-signal protocol.`

Why: Two engine files encode the SSE2-baseline decision in comments because the build never states it; a future '/arch:AVX2' or '/fp:fast' edit (the obvious first thing someone tries for a DSP engine) would be made blind. /fp:fast in particular can fold isnan checks and break the NaN protocol, so the current precise default is load-bearing and should be written down where the flag lives.

Recommendation: In the root CMakeLists, set the flags explicitly with the reasoning: target_compile_options(mforce_warnings INTERFACE /fp:precise) with a comment naming the NaN protocol, and an option(MFORCE_ARCH "SSE2|AVX2" SSE2) that maps to /arch so the ISA choice is a documented switch rather than an absence.

- verifier: Evidence verified exactly: no /arch or /fp in any CMakeLists (grep: zero hits); build/CMakeCache.txt shows stock flags only (CMAKE_CXX_FLAGS='/DWIN32 /D_WINDOWS /EHsc', Release='/O2 /Ob2 /DNDEBUG'), so MSVC defaults (/fp:precise, SSE2 baseline on x64) are what the engine runs under. partials.h:679-681 quote is verbatim; fast_math.h:90 quote is verbatim; partials.h:673 returns quiet_NaN as a cutoff sentinel and it is consumed by std::isnan at partials.h:627 and :231, so the NaN protocol is real and would be at risk under a finite-math-style /fp:fast. What does NOT hold up is the severity/category: nothing is wrong today, no performance is lost (category 'performance' is misapplied), and the SSE2 constraint IS stated in the engine comments, just not next to the flags. Only the /fp:precise dependency is genuinely undocumented. This is a document-the-absent-flag maintenance note with no concrete present cost -> low.
- verifier: Evidence confirmed: root CMakeLists.txt lines 4-17 (and engine/, tools/*) set no /arch and no /fp anywhere (grep over all non-build, non-third_party CMakeLists: zero hits). partials.h:673 returns quiet_NaN as the past-cutoff signal, consumed by std::isnan at partials.h:231 and :627; partials.h:678-681 comment states 'Release flags (/O2, SSE2 baseline, no /arch)' and fast_math.h:90 states 'CRT calls under SSE2 baseline'. So the ISA/FP model is indeed implicit in CMake and documented only in engine comments. The consequence is hypothetical (a future /arch or /fp:fast edit); the int-cast truncf replacement is bit-identical on any ISA so /arch would not break it, only stale the comment. The /fp:fast-folds-isnan risk is documented for GCC/Clang finite-math; on cl.exe std::isnan(float) routes to the CRT bit-classifying isnan, so folding is less certain, though /fp:fast would still cost the bit-exactness partials.h relies on. No CLAUDE.md rule asks for explicit flags; nothing is broken today and the constraint IS written down at the two call sites. Documentation preference, no concrete cost: low, not medium.
- judge: Cost today is zero (MSVC defaults to /fp:precise and the SSE2 baseline the hot-loop code was tuned against), so not high; but the NaN-as-signal gate in partials.h:627/231 (std::isnan skip) would let NaN partials sum into the accumulator and corrupt audio if someone adds /fp:fast, which is the first flag anyone reaches for in a DSP CMake, and nothing at the flag site says not to. A few lines pinning /fp:precise with a comment naming the NaN protocol and the SSE2 decision is cheap insurance. Correction to the recommendation: no `mforce_warnings` INTERFACE target exists in this repo, so the fix as written fails at configure; put it in the existing if(MSVC) block as add_compile_options(/fp:precise) or target_compile_options(mforce_engine PUBLIC /fp:precise) in engine/CMakeLists.txt. The MFORCE_ARCH option is optional polish; the comment + explicit /fp flag is the load-bearing part.

## F002 [CONFIRMED] low (reporter: low) build — CMakeLists.txt:19

**GLFW, RtAudio and RtMidi are built unconditionally even for CLI/render-only configurations; no CMAKE_BUILD_TYPE default for single-config generators**

Reporters: tools-misc-build

Evidence: `add_subdirectory(engine/third_party/glfw) ... add_subdirectory(engine/third_party/rtaudio) ... add_subdirectory(engine/third_party/rtmidi) with no option(MFORCE_BUILD_UI); no 'if(NOT CMAKE_BUILD_TYPE)' anywhere (build/CMakeCache.txt shows the Visual Studio 17 2022 multi-config generator today, which hides this).`

Why: A headless render box or a batch sweep only needs mforce_engine + mforce_cli but pays for the whole UI dependency stack. Separately, a Ninja configure with no build type would silently build the DSP engine without /O2 and without the Release-only /Zi; the current generator masks it.

Recommendation: option(MFORCE_BUILD_UI "Build mforce_ui and its audio/GL deps" ON) wrapping lines 19-41 and the mforce_ui/stamp_test subdirs; and set CMAKE_BUILD_TYPE to Release when unset and the generator is single-config.

- verifier: Evidence verified: lines 24/35/41 add_subdirectory glfw/rtaudio/rtmidi unconditionally; no option() of any kind in any CMakeLists (grep '^option\(' zero hits); no CMAKE_BUILD_TYPE handling anywhere (grep zero hits); CMakeCache confirms Visual Studio 17 2022 multi-config (CMAKE_CONFIGURATION_TYPES=Debug;Release;MinSizeRel;RelWithDebInfo). Consequence (a) is overstated: mforce_engine (engine/CMakeLists.txt) only adds third_party as an include dir and mforce_cli links only mforce_engine; glfw/rtaudio/rtmidi are linked solely by mforce_ui (tools/mforce_ui/CMakeLists.txt:30-37), so a targeted 'cmake --build --target mforce_cli' does not compile them. The cost is confined to configure time (and the directories must exist to configure at all, which is F003's point) plus a default build-all; tools/mforce_ui at line 45 is also unconditional, so a UI-off switch would need to wrap it too (the recommendation says so). Consequence (b) is correct CMake semantics (empty config -> no /O2, no $<CONFIG:Release> /Zi) but purely hypothetical here: no CMakePresets.json, no Ninja or single-config invocation anywhere in docs, scripts, or launch config (only cmake reference is tools/ablate_additive.py pointing at VS's bundled cmake.exe). Finding's own 'low' is right.
- verifier: Evidence confirmed at lines 19-41: glfw/rtaudio/rtmidi add_subdirectory unconditionally, no option(MFORCE_BUILD_UI) (grep: none), no CMAKE_BUILD_TYPE default anywhere, build/CMakeCache.txt shows Visual Studio 17 2022 (multi-config). mforce_cli links only mforce_engine and mforce_engine links none of the three (engine/CMakeLists.txt, tools/mforce_cli/CMakeLists.txt), so only the ALL target or an untargeted `cmake --build` compiles them; docs show builds are almost always `--target mforce_cli/mforce_engine/engine_tests`, with one untargeted invocation. The consequence therefore follows only for untargeted builds, plus a small configure-time cost; the Ninja/no-build-type consequence is correct CMake behavior but no single-config generator is in use. Windows-only scope today makes the headless-Linux angle moot. Low, as reported.
- judge: Both halves are theoretical for this project: it is Windows-only, single-developer, built with the VS multi-config generator (CMakeCache confirms), and no headless render box exists; the extra cost is one-time configure/build of glfw+rtaudio+rtmidi, which is small. mforce_cli does link only mforce_engine, so the MFORCE_BUILD_UI option is correct and concrete (note stamp_test deliberately links nothing, so it need not be inside the option). The CMAKE_BUILD_TYPE default guard is standard hygiene and harmless. Nit-level until a second generator or a headless build appears.

## F003 [CONFIRMED] high (reporter: high) build — CMakeLists.txt:24

**Third-party dependencies are unversioned local checkouts; a fresh clone cannot configure and nothing says why**

Reporters: tools-misc-build

Evidence: `add_subdirectory(engine/third_party/glfw) ... add_subdirectory(engine/third_party/rtaudio) ... add_subdirectory(engine/third_party/rtmidi)  -- while .gitignore has 'engine/third_party/' and README.md has no setup instructions (grep for third_party/clone/vendor: no hits)`

Why: git ls-files shows nothing under engine/third_party; the root CMakeLists has no if(EXISTS)/FATAL_ERROR, no submodule, no FetchContent. A clean clone fails at configure with CMake's generic 'not an existing directory' error. Memory notes record that this directory was wiped once by a worktree removal, i.e. the only copy of the build's dependencies is unprotected local state. For a public release this is the first thing a reviewer hits.

Recommendation: Pin the six dependencies as git submodules (or FetchContent with tagged versions) under engine/third_party, and add an explicit check before the first add_subdirectory: if(NOT EXISTS ${CMAKE_SOURCE_DIR}/engine/third_party/glfw/CMakeLists.txt) message(FATAL_ERROR "run: git submodule update --init"). Document the versions in README.

- verifier: Verified: lines 24/35/41 add_subdirectory the three libs; .gitignore contains 'engine/third_party/' under the comment 'Vendored third-party (local checkouts, not versioned here)'; README.md is 13 bytes ('# MForce') with no setup instructions; no .gitmodules, no FetchContent, no if(EXISTS)/FATAL_ERROR in any CMakeLists (grep zero hits). A fresh clone therefore dies at line 24 with CMake's generic 'not an existing directory' error. One evidence detail is WRONG: 'git ls-files shows nothing under engine/third_party' -- engine/third_party/nlohmann/json.hpp IS tracked (1 file). The five that matter (glfw, imgui, imnodes, rtaudio, rtmidi) are untracked, so the material claim stands; the 'six dependencies' count should be five unversioned + one tracked header. The wipe incident is recorded in the REPO, not just memory: docs/autonomy/dsp/reports/2026-09-16-dsp-run.md:54-62 ('untracked, so unrecoverable from git', 'rtmidi re-vendored at 6.0.0 (version recovered from the build cache)') and docs/autonomy/STATUS.md:624-627 -- i.e. a dependency version was already lost once and had to be reconstructed from a build cache, and docs/superpowers/plans/2026-04-14-composer-strategy-api-refactor.md:58 shows third_party being hand-copied into each worktree. The only setup instruction (git clone --branch 6.0.1 rtaudio) lives in Claude's memory file, not the repo. Severity: the 'public release' framing is aspirational for a solo Windows-only project, but the concrete cost has been paid (data loss of deps, unrecorded versions, per-worktree manual copying) -> high on the rubric's 'concrete cost' definition, not critical.
- verifier: Core claim confirmed: .gitignore:123 ignores engine/third_party/ (comment only says 'local checkouts, not versioned here'), no .gitmodules, no FetchContent, no if(EXISTS)/FATAL_ERROR in any CMakeLists, README.md is literally '# MForce'. A fresh clone fails at add_subdirectory(engine/third_party/glfw) (line 24). One evidence error: `git ls-files engine/third_party` is NOT empty — engine/third_party/nlohmann/json.hpp is tracked, so 5 of the 'six' deps are untracked (glfw, imgui, imnodes, rtaudio, rtmidi), not six. rtmidi is a plain source drop with no .git (version 6.0.0 known only from STATUS.md:625 and memory); glfw/rtaudio/imgui/imnodes are nested clones with no local modifications (git status clean), so data-loss exposure is version pins, not patched code. 'Nothing says why' is true in-repo; the only setup recipe (rtaudio clone --branch 6.0.1) lives in Claude memory, not the repo. The wipe incident is real (STATUS.md:624, memory feedback_no_junctions_into_worktrees) and the standing workaround is a manual robocopy per worktree — a realized, recurring cost, so high is justified. Not critical: no runtime correctness or RT-safety impact, upstream code is re-fetchable.
- judge: This is the real one in the batch: five untracked nested git clones (glfw, imgui, imnodes, rtaudio, rtmidi; only nlohmann/json.hpp is tracked) with no .gitmodules, no FetchContent, no EXISTS check, and an empty README, so a fresh clone or a new worktree cannot configure. The cost is already being paid, not theoretical: memory records engine/third_party being wiped once by a worktree removal (2026-09-16) and a standing rule to hand-copy the dirs into every scratch worktree, plus a git-add workaround that exists only because third_party is untracked. Not critical because no release is imminent and nothing is audible, but it is the first thing a reviewer or a second machine hits. The recommendation resolves it; the current commits are recoverable now via git -C each dir rev-parse HEAD (they are real clones, e.g. rtaudio at 6.0.1), so pinning is a one-shot task. Make the EXISTS guard cover imgui/imnodes too, not just glfw, since tools/mforce_ui compiles them directly via IMGUI_DIR.

## F004 [CONFIRMED] medium (reporter: high) performance — engine/CMakeLists.txt:17

**No precompiled headers or unity build despite a header-heavy engine; each tool TU re-parses the whole engine (15-16 MB objects, /bigobj already required)**

Reporters: tools-misc-build

Evidence: `target_include_directories(mforce_engine PUBLIC ... include ... third_party) with no target_precompile_headers anywhere (grep precompile_headers|UNITY_BUILD: no hits). Build evidence: build/tools/mforce_cli/.../main.obj = 16,281,697 bytes for a 1,246-line main.cpp; mforce_ui main.obj = 15,197,812 bytes; patch_loader.obj = 8,547,695 bytes; tools/mforce_cli/CMakeLists.txt:5 needs '/bigobj'. nlohmann/json.hpp is included from 16 files.`

Why: CLAUDE.md says header-heavy layout is only a smell with a concrete cost; here it is: every tool and test TU pays the full engine header parse plus nlohmann/json on every edit, and the object sizes show it. /bigobj is a symptom of that cost, not a fix. Iteration speed on a project whose workflow is edit-render-listen is directly limited by this.

Recommendation: Add target_precompile_headers(mforce_engine PRIVATE <nlohmann/json.hpp> <vector> <memory> <string> <cmath> <algorithm>) and have each consumer reuse it: target_precompile_headers(mforce_cli REUSE_FROM mforce_engine) (requires matching compile options, which the warnings INTERFACE target below provides). Measure a clean rebuild before/after; if the win is small, the alternative is CMAKE_UNITY_BUILD for the tools.

- verifier: Evidence verified exactly: engine/CMakeLists.txt:17-20 is the PUBLIC include of engine/include + engine/third_party, 22-24 the MSVC /W4 /permissive- block; grep for precompile_headers|UNITY_BUILD over every CMakeLists is empty; build/ object sizes match byte-for-byte (mforce_cli main.obj 16,281,697; mforce_ui main.obj 15,197,812; patch_loader.obj 8,547,695; engine_tests main.obj is 16,376,915 too); tools/mforce_cli/main.cpp is 1,246 lines; tools/mforce_cli/CMakeLists.txt:5 has /bigobj; exactly 16 non-third_party files include nlohmann/json.hpp (10 engine headers, patch_loader.cpp, 5 tool TUs). Consequence confirmed by measurement, not inference: compiling tools/mforce_cli/main.cpp alone with the project's real Release flags (/O2 /Zi /W4 /permissive- /bigobj /std:c++20, same include dirs) takes 95.5 s on this machine; front-end only (/Zs) is 40.8 s; mforce_ui/main.cpp front-end only is 24.4 s. So every consumer TU does pay a large engine-header parse per edit. Two corrections to the argument: (1) object size is a codegen/COMDAT metric, not a parse-time metric, and a PCH neither shrinks .obj files nor removes the /bigobj need; (2) the recommended PCH (<nlohmann/json.hpp> + std headers) measures at 5.1 s front-end on a stub (1.2 s for the std headers alone), so it recovers at most ~5 s of the 95 s (~12% of front-end, ~5% of total). The remaining ~35 s of front-end and ~55 s of codegen are the engine's own headers and the inline code in them; only moving code out of headers (or a PCH of engine headers, which every engine edit invalidates) addresses that, and a unity build does nothing for targets with 1-2 TUs. Diagnosis holds, severity high on measured cost; the fix as written is weak.
- verifier: Evidence verified: engine/CMakeLists.txt:17-24 is the PUBLIC include + /W4 block; grep of all project CMakeLists for precompile_headers|UNITY_BUILD has zero hits (only generated build caches match); build/tools/mforce_cli/mforce_cli.dir/Release/main.obj = 16,281,697 B, mforce_ui main.obj = 15,197,812 B, engine patch_loader.obj = 8,547,695 B (exact); mforce_cli/main.cpp = 1,246 lines; nlohmann/json.hpp included from exactly 16 files (10 engine headers, patch_loader.cpp, 5 tool TUs). Consequence measured, not just asserted: compiling tools/mforce_cli/main.cpp standalone with the build's flags (cl /Bt+, /std:c++20 /O2 /Zi /W4 /permissive- /bigobj) = c1xx 43.1 s + c2 61.4 s; a scratch TU containing only main.cpp's #include lines = c1xx 36.4 s, so ~36 s of every consumer TU is pure header parse/semantic work, repeated across ~20 TUs (13 engine src + 7 tool TUs). Two sub-claims are REFUTED: (1) '/bigobj already required / is a symptom' - both mforce_cli/main.cpp and explore.cpp compile cleanly WITHOUT /bigobj (no C1128), and the 14,658-line mforce_ui/main.cpp is built without it; /bigobj was added in 02680cf (2026-05-23) and is not needed today. (2) Object size as parse evidence is confounded by root CMakeLists.txt:11 adding /Zi in Release: main.cpp obj is 21.7 MB without /Zi vs 42.0 MB with it in my compile. Also the recommended PCH set (<nlohmann/json.hpp> + 5 std headers) measures only c1xx 12.8 s, i.e. it recovers ~13 s of the ~105 s per TU (~12%); the remaining ~24 s is engine headers, which cannot sit in a stable PCH when they are the files being edited. Real, concrete, but modest build-time cost; not correctness/RT-safety, so medium rather than high.
- judge: Downgraded from high: the cost is real (29k lines of engine headers plus nlohmann/json per TU, /bigobj on mforce_cli) but entirely developer-facing and unmeasured — no build timing is given, and object size is a template-instantiation metric, not a parse-time one; in a header-heavy engine where the hot code lives in headers, a PCH only amortizes the stable std/nlohmann set, since any engine header edit still recompiles every TU that includes it. Named cost keeps it in scope under CLAUDE.md. Recommendation is concrete but gated: REUSE_FROM requires identical compile options across engine/cli/ui/tests, which differ today (/bigobj, /W3 vs /W4, none), so it depends on F006's unified warnings target; a lower-coupling alternative is a per-target PCH of <nlohmann/json.hpp> + std headers (no REUSE_FROM, no flag matching), and the biggest zero-infra win is removing nlohmann/json.hpp from public engine headers (the templates.h finding). Measure before/after as the finding itself says.

## F005 [CONFIRMED] low (reporter: medium) build — engine/CMakeLists.txt:17

**mforce_engine is not consumable as a library: C++20 is not a target property, third_party is exported wholesale, and there is no export/install/alias**

Reporters: arch-build-headers

Evidence: `engine/CMakeLists.txt:17-20 `target_include_directories(mforce_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/third_party)`; root CMakeLists.txt:4-5 `set(CMAKE_CXX_STANDARD 20)` (directory-scope only); grep over all CMakeLists for `target_compile_features|install(|export(|ALIAS|CMAKE_CXX_EXTENSIONS` returns nothing`

Why: A future JUCE/plugin consumer doing add_subdirectory(engine) or FetchContent gets no C++20 requirement from the target (47 headers use std::span, so it fails at compile with confusing errors), inherits the entire third_party tree (glfw, imgui, imnodes, rtaudio, rtmidi) on its include path, and has no namespaced target or install rules. CMAKE_CXX_EXTENSIONS unset means gnu++20 on GCC/Clang.

Recommendation: Add `target_compile_features(mforce_engine PUBLIC cxx_std_20)`, `set(CMAKE_CXX_EXTENSIONS OFF)`, `add_library(mforce::engine ALIAS mforce_engine)`, and narrow the PUBLIC include to `third_party/nlohmann` (or better, make nlohmann an INTERFACE target `nlohmann_json` linked PUBLIC until the json dependency leaves the public headers per the templates.h finding). Keep imgui/glfw/rtaudio includes PRIVATE to mforce_ui where they already are.

- verifier: Evidence verified exactly: engine/CMakeLists.txt:17-20 exposes engine/third_party PUBLIC (the directory holds glfw, imgui, imnodes, nlohmann, rtaudio, rtmidi); root CMakeLists.txt:4-5 sets CMAKE_CXX_STANDARD 20 at directory scope only; grep for target_compile_features|install(|export(|ALIAS|CMAKE_CXX_EXTENSIONS over root, engine and all tools/*/CMakeLists.txt is empty; 47 headers under engine/include contain std::span. Consequence follows for the stated hypothetical: a consumer that add_subdirectory(engine)s without setting CMAKE_CXX_STANDARD gets no cxx_std_20 from the target (MSVC default C++14, std::span fails), CMAKE_CXX_EXTENSIONS unset really is gnu++20 on GCC/Clang, and the whole third_party tree lands on its include path. Corroborating latent risk inside the repo: durn_converter and ppl_to_json set CXX_STANDARD 17 and compile engine headers, working only because the subset they include avoids span; nothing enforces it. But there is no consumer today, every current target inherits C++20 from the root, and nothing is broken; the cost is entirely prospective and the fix is three lines. That is hygiene aligned with CLAUDE.md's JUCE/cross-platform direction, not a concrete cost, so low rather than the reported medium.
- verifier: Evidence verified verbatim: engine/CMakeLists.txt:17-20 exports ${CMAKE_CURRENT_SOURCE_DIR}/third_party PUBLIC; root CMakeLists.txt:4-5 set(CMAKE_CXX_STANDARD 20)/REQUIRED (directory variable, not a usage requirement); grep over project CMakeLists for target_compile_features|install(|export(|ALIAS|CMAKE_CXX_EXTENSIONS returns nothing (only hits are generated CMakeCXXCompiler.cmake files in build dirs); exactly 47 headers under engine/include use std::span; engine/third_party contains glfw, imgui, imnodes, nlohmann, rtaudio, rtmidi. Engine headers/src include nothing from third_party except nlohmann/json.hpp, so the wholesale export is unneeded and narrowing to nlohmann is feasible. Consequence follows from CMake semantics: CMAKE_CXX_STANDARD only seeds the CXX_STANDARD property of targets created after it in that directory scope; a foreign project doing add_subdirectory(engine) without setting it compiles mforce_engine at MSVC's default and nothing propagates cxx_std_20 to linkers. In-repo corroboration: tools/durn_converter and tools/ppl_to_json set CXX_STANDARD 17 explicitly and include engine headers (they survive only by avoiding the std::span headers). CMAKE_CXX_EXTENSIONS default is ON, so gnu++20 on GCC/Clang is correct. However there is no cost today: no external consumer exists, nothing is installed/exported, Windows-only is sanctioned by CLAUDE.md. Forward-looking hygiene aligned with the 'JUCE/cross-platform later' direction, a few lines to fix; low.
- judge: Cost is theoretical today: no external consumer exists, every in-tree target gets C++20 from the root directory-scope CMAKE_CXX_STANDARD, and nothing fails now; CLAUDE.md names JUCE compat and cross-platform as direction, so it is in scope as cheap hygiene, not as a current defect. target_compile_features(PUBLIC cxx_std_20), CMAKE_CXX_EXTENSIONS OFF and the mforce::engine ALIAS are one-liners and correct. The 'narrow PUBLIC include to third_party/nlohmann' step as written would break the existing #include <nlohmann/json.hpp> spelling because the layout is third_party/nlohmann/json.hpp (no nested dir); it needs a directory move (e.g. third_party/json/nlohmann/json.hpp with an INTERFACE target exposing third_party/json). Skip install/export rules until a consumer exists.

## F006 [DISPUTED] low (reporter: medium) build — engine/CMakeLists.txt:22

**Warning configuration is inconsistent per target, has no warnings-as-errors, and is MSVC-only**

Reporters: arch-build-headers

Evidence: `engine/CMakeLists.txt:22-24 `if (MSVC) target_compile_options(mforce_engine PRIVATE /W4 /permissive-)`; tools/mforce_ui/CMakeLists.txt:40 `target_compile_options(mforce_ui PRIVATE /W3)` for the 14,658-line main.cpp; tools/stamp_test/CMakeLists.txt:8 `/W3`; tools/engine_tests/CMakeLists.txt:1-3 sets no options at all; tools/mforce_cli/CMakeLists.txt:5 `/W4 /permissive- /bigobj`; no `/WX`, `-Wall`, `-Wextra` or `CMAKE_COMPILE_WARNING_AS_ERROR` anywhere in CMakeLists.txt, engine/CMakeLists.txt or tools/*/CMakeLists.txt`

Why: Header-only code is compiled inside each consumer TU, so the engine's /W4 is effectively downgraded to /W3 or nothing in the two largest consumers (mforce_ui, engine_tests); warnings such as the local-variable shadowing in ks_string.h:271 never surface there. With no /WX, warnings accumulate silently. Nothing applies on GCC/Clang, so the "plan for cross-platform" has no warning baseline to carry over.

Recommendation: Define the flags once in an INTERFACE target (`mforce_warnings`: MSVC `/W4 /permissive- /WX` with a short pragma-free exclusion list, else `-Wall -Wextra -Wshadow -Werror`) and link it into mforce_engine PUBLIC and every tool; mark imgui/imnodes sources as SYSTEM or compile them in their own object library so third-party warnings do not block /WX.

- verifier: Every cited flag line is exact (engine 22-24 /W4 /permissive-; mforce_ui:40 /W3; stamp_test:8 /W3; mforce_cli:5 /W4 /permissive- /bigobj; engine_tests sets nothing; no /WX, -Wall, -Wextra, -Werror or WARNING_AS_ERROR in any CMakeLists; mforce_ui main.cpp is 14,658 lines). The 'nothing' for engine_tests is even right: CMAKE_CXX_FLAGS in the cache is '/DWIN32 /D_WINDOWS /EHsc' with no /W3 (CMP0092) and its vcxproj has no WarningLevel, so it compiles at the compiler default /W1. The stated consequence is what fails. The ks_string.h:271 example: the shadow is real (float d at line 235, redeclared at 271 inside next()) and C4456 fires at /W4 and not at /W3 (verified by compiling a stub both ways), BUT the header is included by exactly one TU in the repo, engine/src/source_registrations.cpp:41, which is compiled inside mforce_engine at /W4, so the warning does surface in the normal build; and mforce_ui never compiles ks_string.h at all, so its /W3 cannot hide it. Generalising with /showIncludes over every /W4 TU (13 engine .cpp + cli main/explore + test_figures) versus mforce_ui main.cpp and engine_tests main.cpp: of the 40 repo headers mforce_ui compiles, 39 are also compiled by a /W4 TU (the only escapee is tools/mforce_ui/build_stamp.h, 261 lines); of the 79 engine_tests compiles, all 79 are. So 'the engine's /W4 is effectively downgraded in the two largest consumers' does not hold for engine headers; what is actually under-checked is the UI main.cpp body at /W3, the engine_tests main.cpp body at /W1, and build_stamp.h. The no-/WX point is factually true (a /W4 compile of cli main.cpp already emits C4189 basics.h:165 and conductor.h:550, C4100 strategy.h:26, C4996 getenv default_strategies.h:91 and composer.h:1343, C4456 composer.h:1301) but warnings-as-errors is a policy preference, not a CLAUDE.md rule, and MSVC-only matches 'Windows-only is fine for now'. The per-target inconsistency could survive as a low hygiene note if the ks_string.h example and the downgrade claim are dropped.
- verifier: Config evidence verified verbatim: engine/CMakeLists.txt:22-24 /W4 /permissive-; tools/mforce_ui/CMakeLists.txt:40 /W3; tools/stamp_test/CMakeLists.txt:8 /W3; tools/engine_tests/CMakeLists.txt has no compile options (2 lines) AND build/CMakeCache.txt CMAKE_CXX_FLAGS='/DWIN32 /D_WINDOWS /EHsc' carries no /W3, so engine_tests compiles at MSVC default /W1; tools/mforce_cli/CMakeLists.txt:5 /W4 /permissive- /bigobj; no /WX, -Wall, -Wextra, -Werror or COMPILE_WARNING_AS_ERROR in any project CMakeLists; mforce_ui/main.cpp = 14,658 lines; ks_string.h:271 'float d = disp_.buf[rd]' does shadow 'float d' at ks_string.h:235 inside the same non-template next() (MSVC emits C4456 for it). BUT the cited example undercuts the stated mechanism: ks_string.h is included by engine/src/source_registrations.cpp, which is compiled under the engine's /W4, and compiling that TU prints 'ks_string.h(271): warning C4456' - so this warning is NOT hidden by the consumers' levels, it surfaces on every engine build and is simply ignored (no /WX). Likewise every engine-header warning that appears when compiling engine_tests/main.cpp at /W4 (basics.h:165, strategy.h:26, default_strategies.h:91, composer.h:1301, composer.h:1343) already prints in the /W4 mforce_cli build. 'Accumulate silently' is wrong - they print every build (7 in cli main.cpp, 4 in source_registrations.cpp). What the inconsistency DOES hide, measured by compiling mforce_ui/main.cpp at /W4 vs /W3: ~20 level-4 warnings in main.cpp's own code (7x C4456 shadowing of 'f'/'cp', 3x C4189, 1x C4100, 8x C4505 unreferenced static functions) plus the two engine-header C4189s; none verified as an actual bug. GCC/Clang point is true but CLAUDE.md explicitly defers cross-platform; /WX is a preference. Factual claims hold, consequence holds only in the weaker 'ui's own W4 warnings are hidden' form; hygiene-only, low.
- judge: Concrete inconsistency: with cmake_minimum_required 3.20 CMP0092 is NEW, so engine_tests compiles the header-only engine at MSVC default /W1 and mforce_ui (14,658-line main.cpp) at /W3, while mforce_cli and the engine lib use /W4 — the two largest consumers silently skip /W4 diagnostics on engine headers. Not user-visible or audible, and the cross-platform half is deferred by CLAUDE.md ('Windows-only is fine for now'), so medium, not high; note the cited ks_string.h:271 example does surface in source_registrations.cpp at /W4 in the engine lib, so warnings are not fully hidden. Recommendation is concrete but should be staged: unify flags via an INTERFACE target first (F004's REUSE_FROM also needs this), compile imgui/imnodes in their own object library or mark their include dirs SYSTEM, clear the existing warning backlog, and only then consider /WX — flipping /WX on in one step will block builds. CMAKE_COMPILE_WARNING_AS_ERROR is the portable switch if the minimum is bumped to 3.24.

## F007 [REFUTED] low (reporter: medium) soundness — engine/include/mforce/core/curve.h:59

**Log-domain curve evaluation (curve.h) and expression-mode (curve_node.h) have no guard against duplicate or non-positive knot x — divide-by-zero / log of non-positive yields inf/NaN**

Reporters: core

Evidence: `curve.h:59-60 `const float lf = std::log(x / px(i - 1)) / std::log(px(i) / px(i - 1));` — only the Linear branch (54-56) got the `w > 0.0f` guard the header comment (19-22) describes. curve_node.h:74 `t = (x - k0.x) / (k1.x - k0.x);` and :76 `std::log(x / k0.x) / std::log(k1.x / k0.x)` likewise divide by zero when adjacent knots share x.`

Why: The parity note claims the duplicate-x division was fixed; it was fixed for one of three domains and not for expression mode. A curve edited in the UI to have two points at the same x (an easy drag outcome) feeds inf/NaN into whatever it modulates (frequency, amplitude), with the same downstream-state poisoning as any NaN.

Recommendation: Apply the same `w > 0 ? ... : 1.0f` fallback to the log ratio, and treat px(i-1) <= 0 in LogX/LogLog as a Linear segment (the expr path already does this at curve_node.h:73).

- verifier: Evidence is at the cited lines (curve.h:59-60, curve_node.h:74/76) but the headline consequence does not follow. In both eval_core and map_expr the segment search is: guard `x <= px(0)` returns py(0), then `for i=1..n-1: if (x <= px(i))`. At the selected i every earlier check failed, so px(i-1) < x <= px(i) holds exactly in float, i.e. px(i) > px(i-1) strictly. A zero-width (duplicate-x) or reversed segment can never be selected, so log(px(i)/px(i-1)) and (k1.x - k0.x) are never zero from duplicate knots; the Linear `w > 0` guard the note describes is itself effectively dead (only FTZ-denormal flush could make w==0). The 'easy drag outcome' claim is also wrong: the shape editor drag (main.cpp:6961-6974) separates x from both neighbours by eps and clamps x >= 1e-4 for logx/loglog, and the Properties table (main.cpp:7411) and paramMap curve table (7154) clamp x >= 0.0001 / 1.0. The only surviving sliver is the non-positive-x sub-claim in curve.h: LogX/LogLog with px(i-1) <= 0 gives log(x/0)/log(px(i)/0) = inf/inf = NaN, and curve.h lacks the `k0.x <= 0 -> linear` fallback that map_expr has at curve_node.h:73. Reachable only by seeding a CurveNode with the default {0,0},{1,1} knots (main.cpp:10750/11175) and flipping the interp combo (7387) to logx/loglog without touching x (NaN for inputs in (0,1] only), or by hand-authored JSON; no shipped library patch exercises it per the parity note. That is a worthwhile 2-line hardening (apply the expr-path non-positive guard in eval_core), not the medium-severity duplicate-x NaN poisoning described. Recommendation part 1 (w>0-style fallback on the log ratio) is unnecessary; part 2 (treat px(i-1)<=0 as Linear) is the valid bit.

## F008 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/core/curve_node.h:62

**CurveNode::map_expr re-implements the bracket search and Linear/LogX/LogLog interpolation that Curve::eval_core (curve.h:45-67) was introduced to share**

Reporters: core

Evidence: `curve_node.h:66-80 `for (size_t i = 1; i < exprKnots.size(); ++i) { if (x <= exprKnots[i].x) { ... t = std::log(x / k0.x) / std::log(k1.x / k0.x); if (interp == CurveInterp::LogLog && v0 > 0.0f && v1 > 0.0f) return v0 * std::pow(v1 / v0, t); return v0 + (v1 - v0) * t;` mirrors curve.h:51-64 line for line, with y replaced by eval_expr(k, x).`

Why: Two copies of the interpolation semantics already diverged (the `k0.x <= 0` fallback exists only in map_expr; the duplicate-x guard only in eval_core's Linear branch). eval_core's accessor design was built precisely so hosts pass lambdas: py = eval_expr(exprKnots[i], x) reproduces expression mode exactly, including edge extrapolation (eval_core returns py(0)/py(n-1) at the edges, which with that lambda is the edge formula evaluated at x).

Recommendation: Implement map_expr as `Curve::eval_core(n, [&](i){return exprKnots[i].x;}, [&](i){return eval_expr(exprKnots[i], x);}, [](size_t){return Curve::Seg{};}, domain, 0.5f, x)` and delete the hand loop.

- verifier: Evidence confirmed at the cited lines. curve_node.h:62-83 map_expr is a hand loop (edge returns at 64-65, bracket search 66-67, LogX t via log ratio at 76, LogLog pow at 77-78, lerp at 79) and curve.h:45-67 eval_core has the same structure line for line (edges 49-50, loop 51-52, log ratio 59-60, LogLog 61-62, lerp 63). Commit a59ef0e ('Hand-rolled interp loops deleted; curve.h eval_core carries the ParamSlot-verbatim semantics forward') routed points-mode map() through Curve::eval but left map_expr untouched, so the duplication is a leftover of that refactor, not a design choice. The two stated divergences are real: the `k0.x <= 0.0f` linear-t fallback (line 73) exists only in map_expr (added in 927ed22), and the w>0 duplicate-x guard + clamp exists only in eval_core's Linear branch. Caveat on the 'reproduces expression mode exactly' claim: the proposed lambda replacement drops the k0.x<=0 fallback, so a LogX/LogLog expression curve with a non-positive left knot would go through log() and NaN instead of linear t. Scanned all 1998 expressions-mode CurveNodes across patches/ (1135 files): every one is interp=linear (1804 single-knot, 198 multi-knot); the 210 nodes with x=0 are all single-knot (edge path), so no shipped patch exercises that fallback and the swap is numerically identical for all of them (SmoothnessInterpolator at 0.5 is exactly a+(b-a)*t, smoothness_interpolator.h:25-26; in-bracket pos is already in [0,1] so the clamp is a no-op). Minor cost of the recommendation: eval_core calls py() up to 5 times in the LogLog branch (each a std::pow for Power knots) and interpolate() computes an unused cos per call; negligible because next() caches on x change (curve_node.h:52). Not a hot-path or rule issue. Severity medium: a real duplicate that has already diverged, inside the module whose shared evaluator was created to prevent exactly this; fix should carry the k0.x<=0 guard into eval_core (or document its removal) rather than silently drop it.

## F009 [CONFIRMED] low (reporter: low) build — engine/include/mforce/core/denormals.h:8

**FTZ/DAZ helper gates on _MSC_VER, which also matches MSVC ARM64 where <immintrin.h>/_MM_SET_* do not exist**

Reporters: arch-render-pipeline

Evidence: ``#if defined(_MSC_VER) || defined(__SSE2__)
#include <immintrin.h>` (8-10) and the same guard around `_MM_SET_FLUSH_ZERO_MODE` (15-18).`

Why: CLAUDE.md asks to plan for cross-platform; the first non-x64 Windows build (ARM64 laptops) fails to compile the engine here, and non-MSVC non-SSE2 targets silently get no denormal protection (the 100x slowdown documented in the file's own comment).

Recommendation: Guard on `defined(_M_X64) || defined(_M_IX86) || defined(__SSE2__)` and add the AArch64 FPCR FZ bit path (or `#error`/no-op with a static_assert comment) so the intent is explicit.

- verifier: Evidence verified verbatim: engine/include/mforce/core/denormals.h lines 8-10 guard `#include <immintrin.h>` on `defined(_MSC_VER) || defined(__SSE2__)`, and lines 15-18 use the same guard around _MM_SET_FLUSH_ZERO_MODE/_MM_SET_DENORMALS_ZERO_MODE, with an empty fallback (no-op on anything else). Consequence confirmed against the installed toolchain: MSVC 14.41 immintrin.h line 14-15 is `#if !defined(_M_IX86) && !defined(_M_X64) && !(defined(_M_ARM64) && defined(USE_SOFT_INTRINSICS)) #error This header is specific to X86, X64, ARM64, and ARM64EC targets`, and line 18-19 additionally errors unless included through <intrin.h> on ARM64/ARM64EC; xmmintrin.h and pmmintrin.h carry the same gates. So a plain MSVC ARM64 build does hit a hard #error at this include. The header is live, not dead: included and called from tools/mforce_cli/main.cpp:1/1215 and tools/mforce_ui/main.cpp:1/3862/12949. Two imprecisions in the finding, neither changing the verdict: (a) the _MM_SET_* macros do exist for ARM64EC/USE_SOFT_INTRINSICS soft-intrinsic builds, the failure is the direct include path, not macro absence; (b) it is the two tool executables that include the header, the engine library sources do not, so 'fails to compile the engine' overstates slightly. The silent no-op on non-MSVC non-SSE2 targets is accurate from the empty #else. No CMakeLists sets any arch flags, so this is the only explicit x86 assumption found outside research/ (research/additive_perf/simd_proto.cpp is a standalone prototype). Severity: low. Repo is Windows-x64-only by CLAUDE.md scope; no ARM64 build exists or is planned near-term; the fix is a 3-line guard change plus optional AArch64 FPCR path. Worth doing when cross-platform work starts, not before.

## F010 [REFUTED] low (reporter: low) modern-cpp — engine/include/mforce/core/dsp_value_source.h:1

**Zero [[nodiscard]] and zero noexcept across engine and tools; move operations not noexcept so vector reallocation copies**

Reporters: arch-modern-cpp

Evidence: `grep over engine/include, engine/src, tools: 0 occurrences of `[[nodiscard]]`, 0 of `noexcept` (excluding third_party); UI s_nodes is a std::vector<GraphNode> whose reallocation hazards are hand-annotated at 10580 ("node creation can reallocate s_nodes"), 10687, 11164`

Why: Accessors like get_param/get_setting/next() can be called and discarded without a warning; without noexcept moves, std::vector growth of node-holding structs falls back to copy, and the UI already has three comments warning about pointer invalidation on emplace_back.

Recommendation: Mark pure accessors and factory functions [[nodiscard]]; declare move ctors/assignments `= default` noexcept on value types held in vectors (GraphNode, Pin, Link, FigureUnit); consider std::deque or index handles for s_nodes.

- verifier: Evidence partly checks out: grep over engine/ + tools/ (excluding third_party) gives 0 `[[nodiscard]]` and 0 `noexcept`, and the three s_nodes reallocation comments exist at tools/mforce_ui/main.cpp:10580, 10687, 11164 (plus 9161). The stated consequence does NOT follow. GraphNode (main.cpp:245-382), Pin (87-112) and Link (636-641) declare only ordinary constructors — no user-declared destructor, copy or move — so the implicit move constructor is generated and its noexcept-ness is deduced from members (std::string, std::vector, std::shared_ptr, std::map, std::unordered_map, nlohmann::json, ImVec2, char[], scalars: all noexcept-movable). std::vector reallocation therefore moves these, it does not fall back to copy; adding `= default noexcept` would change nothing. The three 'emplace_back may reallocate' comments are about pointer/reference invalidation on reallocation, which happens identically whether elements are moved or copied, so they are not evidence for the noexcept claim. FigureUnit (engine/include/mforce/music/figures.h:501-508) is likewise an aggregate of scalars + variant/ornament, same story. What remains is a style preference ([[nodiscard]] on accessors) with no concrete cost shown — low per CLAUDE.md's 'only with a concrete cost' rule.
- judge: Cost is theoretical: discarding get_param/next() results is harmless, and the vector-reallocation hazard the evidence cites (pointers into s_nodes invalidated on emplace_back) happens whether elements are moved or copied, so noexcept moves would not remove the three hand-annotated hazards; GraphNode has no user-declared special members and its members (string/vector/shared_ptr/json) already move noexcept, so the '= default noexcept' change is a no-op. The only part that addresses a real hazard is the tentative 'consider std::deque or index handles for s_nodes', which is a UI-storage finding, not a dsp_value_source.h one; a concrete version would be 'store s_nodes in std::deque (or a stable id->node map) and drop the re-find-after-emplace pattern', filed against tools/mforce_ui/main.cpp.

## F011 [CONFIRMED] medium (reporter: medium) soundness — engine/include/mforce/core/dsp_value_source.h:103

**Silent-no-op class: void set_param/add_param defaults mean the loader cannot detect a rejected wire; three further loader paths swallow bad input silently (Parked.txt item 5)**

Reporters: render-loader

Evidence: `dsp_value_source.h:103 `virtual void set_param(std::string_view, std::shared_ptr<ValueSource>) {}` and 107 add_param — no acceptance signal. Consumers silently drop wrong-typed wires: full_additive_source.h:49-50 `auto fmt = std::dynamic_pointer_cast<IFormant>(src); if (fmt) formant_ = ...`, formant.h:320-321/328-329, additive_source2.h:82-83. In the loader: 362-367/379 `skip_unresolvable` drops any string in a KNOWN pin slot with no message (the unknown-key pass 546-553 does not fire because the key is known); 478-482 an expandRule {"ref"} to a missing id or non-ExpandRuleNode is ignored (contrast resolve_param 116-117 which throws); 1507 `nodeMap.emplace(id, n)` keeps the FIRST duplicate id silently while 1159 `nodeMap[id] = perturbed` keeps the LAST.`

Why: docs/Parked.txt item 5 names exactly this: the UI shows a wire but the DSP has no connection. The loader is the one place every edge passes through, yet it has no way to know a set_param was refused, so a mis-typed pin or a typo'd string value renders a silently different patch — the failure mode the file's own comments call 'exactly the failure mode this loader is full of'.

Recommendation: Change the base interface to `virtual bool set_param(...) { return false; }` / `virtual bool add_param(...) { return false; }` (overriders return true on the matching branch, false at the fall-through); wire_params_generic warns `[load] <type>.<pin>: wire refused` on false. Throw on duplicate node ids in both index sites. Warn on skipped strings unless the (type,key) pair is in the legacy-string list the configurator declares. Throw on an unresolved or wrong-typed expandRule ref.

- verifier: Every cited location verified. dsp_value_source.h:103 `virtual void set_param(...) {}` and :107 `add_param` return void with no acceptance signal. Silent-drop consumers: full_additive_source.h:49-50 (`if (fmt) formant_ = ...`), formant.h:320-321 and 328-329, additive_source2.h:82-83 (dynamic_pointer_cast<Partials>, non-Partials dropped). Loader: patch_loader.cpp:362 defines `skip_unresolvable = is_string`, used at :367 and :379 — a string in a KNOWN pin/param slot is `continue`d with no message, and the unknown-key warning pass at :539-553 only fires for keys absent from the descriptors, so it cannot catch it. :475-481 an `expandRule {"ref"}` to a missing id or a non-ExpandRuleNode falls through silently, versus resolve_param :116-117 which throws on an unresolved ref. :1507 `nodeMap.emplace(id, n)` keeps the first duplicate id, :1159 `nodeMap[id] = perturbed` keeps the last — and both push the id into nodeOrder, so a duplicate would also be built twice; grep finds no duplicate-id check anywhere in patch_loader.cpp. docs/Parked.txt item 5 (lines 33-44) says exactly what the finding says ('the UI shows a wire but the DSP has no connection'). One misattribution: the quote 'exactly the failure mode this loader is full of' lives in engine/src/source_registrations.cpp:273-274 (CombinedSource configurator), not in patch_loader.cpp — but it is a loader-subsystem comment and supports the point. Consequence follows: the loader is the single place every edge passes through and has no way to learn a set_param was refused. Medium: a known, parked design gap (Parked.txt 5), not a release-breaking defect, but it directly feeds Matt's own 'lying wires' complaint.
- judge: User-visible: a wire the UI draws (or a typo'd/mis-typed JSON pin) loads without a word and renders a different patch — the project's own Parked.txt item 5 and pin_model_design.md name this 'silent inertness' as its recurring bug class, and the loader is the single choke point that cannot see the refusal because set_param/add_param return void. Not critical: correct patches render correctly; the flaw is undiagnosed mis-wiring, not corruption. Recommendation is concrete (bool returns, '[load] <type>.<pin>: wire refused' in wire_params_generic, throw on dup ids, throw on bad expandRule ref) though the virtual-signature change is a wide mechanical edit across every override; one caveat — patch_loader.cpp:1159 `nodeMap[id] = perturbed` (keep-last) may be an intentional overlay, so verify before converting both index sites to throw. Overlaps F012 (same root); fold into one item whose fix is F012's status enum + accepts tag plus F011's loader-side throws.

## F012 [CONFIRMED] medium (reporter: medium) soundness — engine/include/mforce/core/dsp_value_source.h:103

**set_param/set_setting/add_param are string-keyed, return void and silently ignore unknown names and wrong-typed sources; structural pins have no type tag**

Reporters: arch-valuesource-graph

Evidence: `virtual void set_param(std::string_view /*name*/, std::shared_ptr<ValueSource> /*src*/) {}

partials.h:1414-1418 (CompositePartials): if (name == "partials") { auto* p = dynamic_cast<IPartials*>(src.get()); if (p) sets_.push_back({std::move(src), p}); }   // non-IPartials silently dropped
multiplex_source.h:163-165: auto cs = std::dynamic_pointer_cast<ConstantSource>(cur); if (cs) cs->set(value);   // silent no-op otherwise
patch_loader.cpp:476-482: if (auto* host = dynamic_cast<Partials*>(&src)) { ... if (auto* node = dynamic_cast<ExpandRuleNode*>(it->second.get())) host->set_expand_rule(...)
patch_loader.cpp:1551-1555: if (auto* env = dynamic_cast<Envelope*>(src.get())) { vg.allEnvelopes.push_back(env); ...`

Why: The project's own design doc names 'silent inertness' as its recurring bug class (pin_model_design.md §5), and the interface institutionalises it: a misspelled pin, a pin that is multi on one node and single on another (MultiSource::set_param appends, CombinedSource's replaces), or a SineSource wired into a 'partials'/'formant'/'expandRule' pin all compile, load and render — as nothing. The loader guards only the names it knows via descriptors; UI wiring (update_node_dsp_unlocked) and push bindings call set_param with names from patch data. InputDescriptor carries no type requirement, so neither the UI nor the loader can refuse an IPartials pin fed by an oscillator; the engine relies on scattered dynamic_casts at load and in hot paths (delay_line_source.h:154, patch_loader.cpp:308).

Recommendation: Return a status from set_param/add_param/set_setting (bool or a small enum: Ok, UnknownName, TypeMismatch) and have the loader and UI treat anything but Ok as a load error / red wire. Add an `accepts` tag to InputDescriptor (e.g. an enum {Value, Partials, Formant, ExpandRule, Envelope}) that CompositePartials, FullAdditiveSource, FormantSpectrum and the trigger binding can declare, so the UI can refuse the wire and the loader can fail loudly instead of dynamic_cast-and-drop. Normalise multi-pin semantics: set_param on a multi pin should either be rejected or documented as 'replace all'.

- verifier: Core evidence verified: dsp_value_source.h:103-104 void string-keyed set_param/get_param; InputDescriptor (:38-43) carries only name/multi/hint — no type requirement. partials.h:1414-1418 CompositePartials drops non-IPartials silently; multiplex_source.h:163-165 `if (cs) cs->set(value)` silent no-op; patch_loader.cpp:475-481 expandRule dynamic_cast-and-drop; :1551-1555 Envelope dynamic_cast scan. docs/pin_model_design.md:123 (§5) states verbatim 'Silent inertness is the failure mode to guard against ... this project's recurring bug class'. Multi-pin semantic mismatch confirmed: multi_source.h:30-32 set_param("source") APPENDS via add(), combined_source.h:43-46 set_param REPLACES. UI wiring confirmed: main.cpp:935-985 update_node_dsp_unlocked calls wire_pin/add_param with pin names from the loaded graph. One sub-claim refuted: 'dynamic_casts ... in hot paths (delay_line_source.h:154, patch_loader.cpp:308)' — delay_line_source.h:154 is inside walk_to_tap, which is called only from prepare() (line 97), not next() (line 101); patch_loader.cpp:308 is load-time promote_starved_refs. Neither is a per-sample path, so no CLAUDE.md hot-loop concern. Note this finding is substantially the same defect as F011 (same base-interface claim, same 'return a status' remedy) with an added 'accepts' tag proposal; the orchestrator should fold them. Medium, same reasoning as F011.
- judge: Same root as F011 (void, silently-dropping set_param) but the broader and better fix: a status return lets the loader fail loudly, and an `accepts` tag on InputDescriptor lets the UI refuse a SineSource->partials/formant/expandRule wire at connect time instead of drawing an inert one — the piece that actually closes Parked item 5. The multi-pin inconsistency is concrete too (CompositePartials::set_param appends while add_param forwards to it, so single-set on a multi pin accumulates instead of replacing). An enum tag on an explicit descriptor is consistent with the 'explicit registries, no reflection' rule, so in scope. High not critical for the same reason as F011: mis-wired patches render silently wrong, correct ones are unaffected. Dedupe with F011.

## F013 [CONFIRMED] medium (reporter: high) soundness — engine/include/mforce/core/dsp_value_source.h:146

**Shared-node sharing model is wiring-order dependent: a RefSource consumer evaluated before the advancer reads the previous sample**

Reporters: arch-valuesource-graph

Evidence: `// The primary consumer calls next() on the real source; secondary consumers
// use a RefSource which just reads current() without advancing. ... float next() override { return read(); }  // read() = source->current()

patch_loader.cpp:118-122: int& count = (*usage)[refId]; count++; if (count > 1) return std::make_shared<RefSource>(it->second);

combined_source.h:89-93: source1_->next(); source2_->next(); float v1 = source1_->current(); float v2 = source2_->current();`

Why: Which consumer owns the raw pointer is decided by JSON nodeOrder (first mention wins, patch_loader.cpp:258-263). If shared source S is wired to pin source1 of node A and pin source2 of node B and B precedes A in nodeOrder, A holds a RefSource and CombinedSource::next() evaluates A before B: A reads S.current() BEFORE B advances S, so A sees sample n-1 while B sees sample n. Reverse the node order in the file and the lag moves to the other branch. For audio-rate shared sources (noise into two branches, a loop tail feeding two filters) this is a hidden z^-1 that flips when the UI re-serialises nodes in a different order. The same fragility is why the engine needed promote_starved_refs (patch_loader.cpp:149-335, a 190-line JSON-plus-graph walk whose first version double-advanced all 56 library voice patches per its own comment at :164-165), collect_advance_ids + advanceList (patch_loader.cpp:178-221, instrument.h:155-160) and a UI mirror of the auto-wrap (mforce_ui/main.cpp:1022-1077). All of it exists to guess who advances.

Recommendation: Make next() idempotent per tick in the base class: add a tick counter to RenderContext/a per-voice clock, store `lastTick_` in ValueSource, and have a non-virtual `pull(tick)` that calls the virtual `advance()` only when `lastTick_ != tick`, otherwise returns the cached value. Every consumer then calls pull(); sharing needs no RefSource, no usage counter, no advancer promotion, and the ordering lag disappears (each shared node evaluates exactly once per tick, on first touch). Keep the guarded tap RefSource as the one explicit previous-sample primitive (its z^-1 becomes well-defined: 'value from the previous tick'). The advance list for tap-only tails stays, but as the only special case. This is a cross-cutting change; plan it with a null gate over patches/library/.

- verifier: Evidence verified. dsp_value_source.h:146-148 comment ('primary consumer calls next() ... secondary consumers use a RefSource which just reads current() without advancing'), :163 `next() { return read(); }`, :170-171 read() = source->current(). patch_loader.cpp:118-122 usage counter wraps the 2nd+ consumer in RefSource. combined_source.h:88-93 calls source1_->next() then source2_->next() then reads both current(). patch_loader.cpp:258-263 advancer = first node mentioning the ref in nodeOrder; the loader's own comment at :169-170 says 'wiring order across nodes IS nodeOrder'. promote_starved_refs comment :149-170 with the 'all 56 library voice patches' double-advance at :163-165, collect_advance_ids :177-221, instrument.h:155-160 advanceList, UI mirror main.cpp:1020-1077 update_all_dsp all exist. The traced consequence holds: with S wired to A.source1 and B.source2 and both A and B pulled by a parent that evaluates A first, if B precedes A in nodeOrder then A holds the RefSource and reads S.current() (sample n-1) before B's next() advances S to n; reversing nodeOrder moves the lag to B. Two qualifiers: (1) the UI picks its advancer by s_links order (main.cpp:1067-1076 'first consumer keeps real source'), not nodeOrder, so UI playback and CLI render can disagree on who advances — this strengthens the 'all of it exists to guess who advances' point; (2) 'flips when the UI re-serialises' is overstated for a plain load/save round trip (nodes are appended in file order at main.cpp:1733 and emitted in s_nodes order), but replace_node (11164-11165) appends the replacement at the back and delete+recreate reorders, so drift is real. Severity: I call medium, not high. The sharing model is deliberate and documented (RefSource comment :152-154 calls the z-1 'positional'; memory 'RefSource advancer model'), the lag is a single sample on the secondary branch (inaudible for envelope/LFO shares, a 1-sample comb for audio-rate shares), and no audible defect is demonstrated; the concrete cost is the ~250 lines of promotion/advance-list machinery, which already works. The recommended tick-idempotent pull is a cross-cutting rewrite that needs the library null gate — worth planning, not an emergency.
- judge: Structural cost is already paid and documented: promote_starved_refs (~190-line JSON+graph walk), collect_advance_ids + advanceList, the UI auto-wrap mirror, and a logged incident where the first cut double-advanced all 56 library voice patches — all machinery to guess who advances. The hidden one-sample lag is real (CombinedSource pulls source1 then source2, so a RefSource on the earlier branch reads sample n-1) but deterministic per file and the UI saves nodes in vector order, so it only flips on delete/recreate/paste; audibly it is a phase offset between branches, or a one-sample loop-length change where a shared source sits in a loop — so high, not critical. The tick-stamped pull()/advance() design is concrete and allocation-free (no CLAUDE.md conflict; the advancer model is a design, not a rule), but it is a rename of next() across every subclass and consumer plus prepare/reseed/voice-clone interplay, and tap semantics change under it: a tap on a node already pulled earlier in the same tick reads z^0 instead of the current positional z^-1, which shifts loop length by one sample in some topologies — the loop family (the best-sounds milestone) is where the null gate over patches/library/ will flag. Define tap semantics ('value from previous tick' vs 'last computed') in the spec before implementation; per the discuss-before-refactoring rule this needs Matt's go, not a run.

## F014 [CONFIRMED] critical (reporter: critical) rt-safety — engine/include/mforce/core/dsp_wave_source.h:38

**WaveSource::next() throws from the per-sample path; the live audio callback has no handler, so a non-positive frequency terminates the process from the audio thread**

Reporters: arch-modern-cpp, arch-render-pipeline, arch-valuesource-graph, core

Evidence: `if (f <= 0.0f) throw std::runtime_error("WaveSource: non-positive frequency");  — pulled at tools/mforce_ui/main.cpp:3890 `float vs = voice.source->next() * voice.gain;` inside audio_callback (3857-3946) with no try/catch`

Why: Exception throw allocates the exception object and unwinds; an exception escaping RtAudio's C callback calls std::terminate. Every oscillator (Sine/Saw/Pulse/FM/Wavetable/Additive/RedNoise/HybridKS derive from WaveSource) inherits this. A legal graph reaches it: any envelope or curve wired straight into `frequency` starts at 0 on sample 0, and a guarded RefSource tap can deliver a negative value. This is both a project-rule violation (no unbounded work in render paths) and a crash path in the node-editor's core use case.

Recommendation: Never throw from next(). Clamp f to a small positive floor (or hold the previous phase increment and emit 0) and keep the non-positive-constant check at load time in patch_loader where it can report by node name.

Also reported as: WaveSource::next() throws on the render thread and the UI audio callback has no exception boundary | WaveSource::next() throws std::runtime_error; this is reachable from the RtAudio callback with no handler | WaveSource::next() throws from the per-sample path; the RtAudio callback has no catch, so a non-positive live frequency terminates the process

- verifier: Evidence exact: engine/include/mforce/core/dsp_wave_source.h:38 `if (f <= 0.0f) throw std::runtime_error(...)` sits in WaveSource::next() ahead of the ptr_==0 init, so it fires on sample 0. All 12 WaveSource subclasses (Sine/Saw/Pulse/Triangle/FM/Wavetable/RedNoise/HybridKS/BasicAdditive/FullAdditive/AdditiveSource2, per grep of `: WaveSource`) inherit it — none override next(). Caller path confirmed: tools/mforce_ui/main.cpp:3890 `float vs = voice.source->next() * voice.gain;` is inside audio_callback (defined 3857); grep shows no try/catch between 3857 and the next try at 4014. RtAudio side: engine/third_party/rtaudio/RtAudio.cpp:5997 invokes the user callback with no handler, on a thread made by CreateThread (5314); the UI's SetUnhandledExceptionFilter (main.cpp:12841/12950) writes a crash log and returns EXCEPTION_EXECUTE_HANDLER, so the process dies. No UI-thread probe catches it first: the note-on path (main.cpp:4688-4699) prepares the voice and explicitly does NOT render — first next() is on the audio thread. Reachability of f<=0 on a legal graph confirmed: Envelope::next() at ptr_=0 gives pos=0 → Ramp::value(0) returns startVal (ramp.h:24), 0.0f for every attack stage (make_ar/make_adsr), mapped to lo=0 when no minValue is set (envelope.h:371-382) — an envelope on the frequency pin throws on the first sample; a guarded RefSource clamps to [-8,8] (dsp_value_source.h:170-174), so a tap on frequency can deliver <=0. perform_source.h:111-119 records this exact throw having fired in practice when one PerformNode fed two consumers. Rule check: CLAUDE.md asks for real-time safety; the throw heap-allocates the runtime_error string and unwinds — violation in addition to the crash. Severity critical: process termination from the audio thread on a wiring the node editor permits.
- verifier: Evidence verbatim at dsp_wave_source.h:38. Call path confirmed: tools/mforce_ui/main.cpp:3857 audio_callback -> :3890 voice.source->next() (and :3940 g_streamSource->next()), with no try/catch anywhere in 3857-3946; nearest handlers are 1024/1081 and 4014/4021, both outside the callback. RtAudio's WASAPI trampoline (engine/third_party/rtaudio/RtAudio.cpp:5997 inside wasapiThread) also has no try, and WASAPI is the compiled backend (CMakeLists.txt:27), so the exception leaves the thread function and the process dies. Inheritance confirmed: all WaveSource subclasses (I count 11, not 12: Sine, Saw, Pulse, Triangle, FM, Wavetable, RedNoise, HybridKS, FullAdditive, BasicAdditive, AdditiveSource2) inherit next() with no override, so every one hits line 38. Reachability from a legal graph confirmed: WaveSource::set_param accepts any ValueSource for "frequency" (lines 84-88); the UI's wire_pin (main.cpp:470-472) forwards any link to set_param with no category filter; the UI Envelope node is make_adsr with Ramp.startVal defaulting to 0 (ramp.h:16, :24 returns startVal at pos<=0; envelope.h:263 forces first-stage start 0) and null min/max = identity, so envelope->frequency delivers 0 on sample 0. Strongest corroboration: perform_source.h:117-119 documents this exact throw firing in practice the first time a PerformNode fed two consumers on a converted patch. The UI clamp at main.cpp:9511 guards only constant-field edits to the 0.01 descriptor floor, not wired sources. One inaccuracy in the recommendation, not the claim: no load-time non-positive-frequency check exists anywhere (grep hits only the throw and the perform_source comment), so there is nothing to "keep" in patch_loader; it would have to be added.
- judge: Confirmed in context: the throw at dsp_wave_source.h:38 runs before any state init and every oscillator inherits it; audio_callback (main.cpp:3857-3946) pulls voice.source->next() and g_streamSource->next() with no try/catch, so an escaping exception from RtAudio's thread is std::terminate — a hard process kill from the node editor's core live-play path, and it has already fired in practice (perform_source.h:117 records the message surfacing when a PerformOut fed two consumers). An LFO or 0-starting envelope wired to frequency is an ordinary graph, not a corner case. Recommendation is concrete and sufficient: drop the throw, clamp f to a positive floor (needed anyway because subclasses derive loop lengths / table steps from sampleRate/f; the phase accumulator itself already tolerates negative increments via the <0 wrap), and move the constant-frequency check to patch_loader with the node name. Keep a debug-only assert so the diagnostic value the throw has been serving is not lost.

## F015 [CONFIRMED] medium (reporter: medium) performance — engine/include/mforce/core/dsp_wave_source.h:59

**Per-sample std::fmod in every oscillator's phase wrap — a CRT call in the hottest loop the project has already profiled against**

Reporters: core

Evidence: `currPos_ = std::fmod(currPos_ + currPhaseIncr_ + phaseDelta, 1.0f);  — runs once per sample for every WaveSource-derived node (12 oscillator types). fast_math.h:65 and :89-90 document that truncf/exp2 CRT calls were measured as the dominant cost in the additive loop.`

Why: fmod has no SSE instruction and is a library call with its own branchy reduction; for the common case |increment + delta| < 1 a compare-and-subtract wraps exactly. Multiplied across voices and partials this is the same class of cost the project already paid to eliminate elsewhere.

Recommendation: Fast path: `p += incr + delta; if (p >= 1.f) p -= 1.f; else if (p < 0.f) p += 1.f;` and fall back to fmod only when fabs(incr + delta) >= 1 (large phase jumps). Verify with the additive null gate.

- verifier: Evidence exact: dsp_wave_source.h:59 `currPos_ = std::fmod(currPos_ + currPhaseIncr_ + phaseDelta, 1.0f);` runs once per sample in WaveSource::next(), which all 12 oscillator subclasses inherit (none override next()). Engine builds with /O2 + /W4 /permissive- only (engine/CMakeLists.txt:23, no /arch, no /fp:fast), under which MSVC x64 emits a CRT call for fmodf — same situation partials.h:676-687 and fast_math.h:65, :89-90 document for truncf/exp2 (the cited fast_math lines say exactly what the finding quotes). The project's own measurement (partials.h:685-687, research/additive_perf/simd_proto.cpp 'sc+cast' rung) found a per-partial truncf CRT call cost 1.4-1.5x on the partial loop body, so the 'same class of cost' claim has backing. REFUTED in part: 'Multiplied across voices and partials' is wrong on partials — the per-partial loop in partials.h already wraps with an int cast (x - trunc(x)), and WaveSource's fmod runs once per NODE per sample regardless of partial count, so for an additive node it is one call against ~96 partial evaluations; the title's 'the hottest loop the project has already profiled against' overstates it (the profiled loop is the per-partial loop; WaveSource::next is the per-node wrapper). For simple oscillators the fmod sits beside std::sin (sine_source.h:15) and a second fmod in PulseSource (pulse_source.h:68), both also CRT calls, so removing it alone does not change the class of the node's cost. No measurement of this call exists anywhere in research/additive_perf or tools/ablate_layers.py (grep for fmod/WaveSource: none). The proposed compare-and-subtract is exact for |incr+delta|<1 (frequency descriptor caps at 20 kHz, incr<0.42 at 48k; phaseDelta only exceeds 1 under extreme phase modulation) and the recommendation keeps an fmod fallback, so correctness is fine. Severity low: real but unmeasured, roughly one CRT call per oscillator node per sample (~sub-percent CPU at typical polyphony), not a demonstrated concrete cost; upgrade to medium only if a simple-oscillator polyphony profile shows it.
- verifier: Evidence verbatim at dsp_wave_source.h:59; fast_math.h:65 (truncf-is-a-CRT-call) and :89-90 (CRT calls under SSE2 baseline) say what the finding says; all 11 WaveSource subclasses (finding says 12) run this fmod once per node per sample with no override. Measured under the engine's flags (MSVC 14.41, /O2, no /arch, loop-carried chain like the engine): fmodf wrap 12-14 ns/sample vs compare-subtract 2.0-2.2 ns/sample, so ~10 ns saved per oscillator node per sample. Two overstatements: (1) WaveSource::next() is NOT the loop the project profiled; that was the per-partial loop in partials.h, whose own phase wrap was already converted to x - float(int(x)) at partials.h:697-701 with a measured 1.4-1.5x; (2) partials keep their own accumulators, so this fmod is multiplied across voices x oscillator nodes only, not across partials. Scale: ~0.5% of a 96-partial additive node's per-sample cost (partials.h:691 cites ~25 ns/partial), roughly 3% of a core for a dense 16-voice live pool. Concrete but small, hence medium not high. Caveat on the recommendation as written: `p += incr + delta` rounds (incr + delta) first, whereas the engine computes (currPos_ + currPhaseIncr_) + phaseDelta; my 5M-step check shows 1,295,874 mismatches for the finding's form and 0 for `p = p + incr + delta` with the engine's association (|incr+delta| < 1). The fix is bit-exact only if association is preserved, and the |incr+delta| >= 1 fallback is genuinely needed because phase spans [-1,1] so phaseDelta can reach +-2.
- judge: Real but bounded cost: std::fmod is a CRT call per oscillator node per sample (not per partial — partials.h has its own loop), so across voices x oscillator nodes at 48 kHz it is low-single-digit percent, not the 65% cliff fast_math.h:59-65 measured for exp2 in the additive loop. The same project rationale applies (CRT calls under SSE2 baseline), so medium is right. The proposed compare-and-subtract is bit-identical to the current fmod + <0 correction for |incr+delta| < 1 (Sterbenz: x-1 exact on [1,2); the x+1 path rounds identically to today's post-fmod add), so the null gate should pass unchanged; the fmod fallback for large phase jumps is the correct guard. Low-risk, mechanical change.

## F016 [REFUTED] low (reporter: medium) efficiency — engine/include/mforce/core/envelope.h:554

**Envelope embeds two std::mt19937 engines and pulls <random> into 72 of 128 headers, while the rest of the engine uses the project's own Randomizer**

Reporters: arch-build-headers

Evidence: `envelope.h:6 `#include <random>`; :554-555 `std::mt19937 rng_;       // for stage_accuracy duration jitter` / `std::mt19937 lfo_rng_;   // for ramp_accuracy LFO`; :338 `std::uniform_real_distribution<float> dist(stage_accuracy, 1.0f);`; by contrast partials.h:986-987, wave_evolution.h:90, ks_string.h:401 all use `Randomizer`. Include-closure script: 72 of 128 headers transitively include <random> (6 headers include envelope.h directly, and it is in the closure of 2,615-line instrument.h and every source that takes an envelope).`

Why: Each mt19937 is ~2.5 KB of state, so every Envelope instance carries ~5 KB of RNG for two rarely-enabled jitter features; a voice pool with several envelopes per voice multiplies that in cache-hot memory. <random> is also one of the heavier STL headers and reaches more than half the engine through this one file. The engine already standardised on Randomizer for determinism and seed storage.

Recommendation: Replace both engines with `Randomizer` (seeded from seed_ and seed_ ^ 0xDEADBEEF exactly as now; `Randomizer::range(lo,hi)` replaces uniform_real_distribution), drop `#include <random>`. Renders change bit-for-bit only for patches with stage_accuracy/ramp_accuracy < 1, which is the documented no-back-compat policy.

- verifier: Evidence partly checks out: envelope.h:6 includes <random>; :554-555 hold two std::mt19937; :338 uses uniform_real_distribution; partials.h:986-987, wave_evolution.h:90, ks_string.h:401 use Randomizer (those files live under engine/include/mforce/source/, not core/). sizeof(mt19937) is ~2.5 KB on MSVC, so ~5 KB per Envelope is right as a fact. But the stated consequences do not follow. (1) The 72-of-128 figure is misattributed: an include-closure script over engine/include confirms 72 headers reach <random>, but envelope.h is in the closure of only 9 of them; 63 reach <random> through randomizer.h, which itself does `#include <random>` (randomizer.h:2). Dropping the include from envelope.h moves 72 to 65, so 'reaches more than half the engine through this one file' is false. (2) The recommendation cannot deliver the memory or header savings: Randomizer wraps its own std::mt19937 (randomizer.h:101) plus a distribution, so two Randomizers cost the same ~5 KB and still pull <random>. (3) Supporting numbers are wrong: envelope.h has 4 direct header includers (envelope_json.h, envelope_presets.h, pitch_bend.h, perform_source.h), not 6; instrument.h is 749 lines, not 2,615. (4) 'cache-hot memory' is overstated: rng_ is touched only in prepare() and lfo_rng_ only at LFO segment starts (every ~N/10 frames), never per sample. What survives is a consistency nit (Randomizer elsewhere) and a bit-for-bit behavior change under the no-back-compat policy.
- verifier: Evidence partly checks out: envelope.h:6 `#include <random>`, :554-555 two std::mt19937, :338 uniform_real_distribution, and partials.h:986-987 / wave_evolution.h:90 / ks_string.h:401 do use Randomizer. An include-closure script over engine/include confirms 72 of 128 headers reach <random>. But the attribution and the stated consequences are wrong: (1) randomizer.h itself does `#include <random>` (randomizer.h:2) and is in the closure of 65 of the 128 headers, so only 7 headers (envelope.h, envelope_json.h, envelope_presets.h, pitch_bend.h, perform_source.h, instrument.h, patch_loader.h) get <random> solely via envelope.h, and the recommended fix (use Randomizer) would keep <random> in every one of them. (2) Randomizer IS a std::mt19937 plus a uniform_real_distribution<float> plus a seed (randomizer.h:41,101-102), so swapping the two engines for two Randomizers leaves the ~5 KB per Envelope unchanged (slightly larger). (3) Two factual errors: instrument.h is 749 lines, not 2,615; envelope.h is included directly by 4 headers (envelope_json.h, envelope_presets.h, pitch_bend.h, perform_source.h) plus 4 .cpp files, not 6 headers. What survives is a pure consistency nit (raw mt19937 vs the project wrapper around the same mt19937); neither the header-weight nor the memory consequence follows from this file or is fixed by the recommendation.
- judge: Cost is theoretical: the ~5 KB per envelope is cold bytes (touched only when stage_accuracy/ramp_accuracy < 1, and no patch under patches/ sets either below 1), and <random> reaches the closure through randomizer.h regardless. The recommendation does not deliver either claimed saving — Randomizer itself wraps a std::mt19937 and #includes <random> (randomizer.h:2,101), so swapping buys idiom consistency only. What would: give Randomizer a small-state generator (xoshiro/PCG, 8–16 B) and keep <random> out of headers — but that changes every seeded source bit-for-bit (keeper WAVs in renders/library/), so it is an engine-wide decision to raise with Matt, not a local fix.

## F017 [CONFIRMED] medium (reporter: high) god-class — engine/include/mforce/core/envelope.h:16

**Envelope is a ~560-line accretion of five orthogonal concerns with ~30 state fields and three timing semantics interleaved in prepare()**

Reporters: core

Evidence: `prepare() alone resolves `absolute_time ? percent : duration*percent`, then `if (gated_ && nominal > 0) stgDur = nominal`, then `*= timeScale_`, then stage_accuracy jitter, then two different clamp rules (327-344); the class also owns gate_release/retrigger, two mt19937 RNGs + a cosine LFO, minValue/maxValue range mapping, `has_adsr_shape_()` shape-sniffing for sustain rewrite (233-240), and the make_ar/make_adsr/make_adsr_abs factories.`

Why: Every new note-model feature (nominal, seconds mode, reflection allowance, release re-layout, retrigger) has been layered into the same class, and the drift is already visible: gate_release's re-layout (109-117) reimplements a subset of prepare's stage math and silently omits two of its terms. Reasoning about which of the clamps applies to a given stage requires reading all of prepare(); the sustain rewrite depends on a structural guess about stage count. This is the class most patches depend on and the one hardest to change safely.

Recommendation: Split into (1) a value-type StageLayout {stages, timing mode} with one `frames_for(stage, refSec, gated)` function used by both prepare() and gate_release(); (2) a runtime cursor (ptr/stage/gate anchor) that walks a prepared count table; (3) a Decorrelation struct (jitter RNG + LFO) applied as a post-pass; move the make_* factories into envelope_presets.h next to their only callers.

- verifier: Descriptive facts verified: the class is 562 lines; exactly 30 state fields (23 private at 528-559, plus public stage_accuracy, timeScale_, ramp_accuracy, minValue_, maxValue_, absolute_time, trigger_); prepare() 327-344 interleaves the fraction / absolute_time / gated-nominal semantics, timeScale_, the jitter draw and two clamp rules; gate_release 109-117 re-derives stage length and omits the nominal override and the stage_accuracy draw (also the reflection-allowance subtraction, which the finding does not mention). has_adsr_shape_ (233-240) checks an exact 4-stage invariant, not a 'guess'. One factual error: 'move the make_* factories next to their only callers' in envelope_presets.h is wrong; make_ar/make_adsr/make_adsr_abs are also called from envelope_json.h:21/39/49, source_registrations.cpp:209 and mforce_ui/main.cpp:411. Severity 'high' is unsupported: the only concrete cost named is the gate_release drift, which is its own finding (F020) and currently affects no tracked patch (no library/baselines patch sets nominal; none sets stage_accuracy below 1 as a literal). The rest is a decomposition preference, and the proposed three-way split is exactly the kind of large rewrite the project's 'discuss before refactoring' rule gates. Style-level call: low.
- verifier: Facts verified: file is 563 lines; exactly 30 state fields (7 public incl. stage_accuracy/timeScale_/ramp_accuracy/minValue_/maxValue_/absolute_time/trigger_, 23 private); prepare() 327-344 interleaves fraction-vs-seconds (327-328), gated nominal override (332-333), timeScale (334), jitter (337-340), and two clamp rules (341-343); has_adsr_shape_() 233-240 is a structural 4-stage guess; two mt19937 + cosine LFO (509-526); min/max range mapping (372-380); gate_release/retrigger; make_ar/make_adsr/make_adsr_abs factories (448-496). The cited drift symptom is real: gate_release's re-layout (113-116) omits the nominal override, the stage_accuracy jitter AND the reflection-allowance subtraction that prepare applies (see F020). One recommendation detail is wrong: envelope_presets.h is not the factories' only caller - envelope_json.h:21/39/49, source_registrations.cpp:209 and tools/mforce_ui/main.cpp:411 also call them. Severity demoted from high to medium: no RT-safety or correctness flaw by itself; the concrete costs it enables are the low-severity F020 drift and the F021 reset, and CLAUDE.md/memory require a discussed design before a refactor of this size.
- judge: The debt is real and the drift is demonstrated (F020's re-layout, the has_adsr_shape_ sniffing, F021's carry-over whitelist), but its cost is latent today: no patch exercises stage_accuracy<1 or nominal, so nothing audible diverges on library/ — medium, not high. The three-way split is concrete but is a large rewrite of the most-depended-on class with corpus bit-exactness risk; per the discuss-before-refactoring rule the incremental path is F020's shared layout helper + F021's layout-as-value-member first. One sub-item is wrong: the make_* factories are not only called from envelope_presets.h — envelope_json.h:21/39/49, source_registrations.cpp:209 and mforce_ui/main.cpp:411 call them too, so they must stay on Envelope.

## F018 [CONFIRMED] low (reporter: low) smell — engine/include/mforce/core/envelope.h:44

**Public members with private-style trailing-underscore names, and a mis-encoded comment**

Reporters: arch-modern-cpp, core

Evidence: `envelope.h:44 `float timeScale_{1.0f};` (public), envelope.h:233 `bool has_adsr_shape_() const` (public), envelope.h:53-54,159 public `minValue_`/`maxValue_`/`trigger_`; randomizer.h:41 public `uint32_t seed_;`; denormals.h:5 contains a replacement character: `runs ~100x slower � profiled`.`

Why: The unit otherwise uses the trailing underscore to mean private; mixed use makes a reader guess the access level at every call site, and the mojibake shows the file was saved in a different encoding than the rest of the tree (a UTF-8 em dash elsewhere).

Recommendation: Rename public fields without the underscore (or make them private with accessors); re-save denormals.h as UTF-8 with the intended em dash.

Also reported as: Public data members with private naming, and inconsistent constexpr-ness of descriptor tables

- verifier: All cited locations verified: Envelope is a struct with no access specifier until `private:` at line 506, so timeScale_ (44), minValue_/maxValue_ (53-54), trigger_ (159) and has_adsr_shape_() (233) are public; randomizer.h:41 `uint32_t seed_;` is public. denormals.h:5 does not literally contain U+FFFD: od shows a single 0x97 byte (Windows-1252 em dash) and `file` reports 'Non-ISO extended-ASCII text', while envelope.h is UTF-8 with 24 UTF-8 em dashes, so the 'saved in a different encoding' conclusion is correct even though the 'replacement character' wording is imprecise. The 'unit otherwise uses trailing underscore to mean private' premise is only mostly true: a scan found other public trailing-underscore members at name_gate.h:26 (in_) and instrument.h:295/392/399, so the convention is already mixed beyond envelope.h. Naming/encoding nit with no runtime cost.
- verifier: All evidence confirmed: `float timeScale_{1.0f};` at envelope.h:44 is public (the `private:` label is at 506); `bool has_adsr_shape_() const` at 233 is public (as is `next_raw_()` at 386); `minValue_`/`maxValue_` at 53-54 and `trigger_` at 159 are public; randomizer.h:41 `uint32_t seed_;` is public. denormals.h line 5 contains a single byte 0x97 (octal 227, cp1252 em dash) rather than UTF-8 E2 80 94 - verified with od; it renders as U+FFFD. Style-only; no concrete cost.
- judge: The rename is a style preference with no concrete cost (25 of the 28 uses are inside envelope.h; 3 external in patch_loader/instrument — churn in the core envelope for readability only), which the scope rule excludes. The one real item is the mojibake in denormals.h:5, a one-character comment fix worth taking in passing but not a finding on envelope.h.

## F019 [CONFIRMED] low (reporter: low) workaround-hack — engine/include/mforce/core/envelope.h:73

**A physical-model policy (reflection allowance) is baked into every Envelope as an engine-wide constant**

Reporters: core

Evidence: `static constexpr float kReflectionAllowanceSec = 0.010f;  used at 302-303: `const int allowFrames = int(kReflectionAllowanceSec * float(sampleRate_)); const int layoutFrames = std::max(0, frames - allowFrames);``

Why: Every envelope in every patch, including pure-synth timbre/modulation envelopes with nothing to disperse, is laid out 10 ms short, and the value cannot differ per instrument. The envelope is reasoning about a property of the instrument that contains it.

Recommendation: Have the instrument pass the layout window (frames minus its own allowance) into prepare(), or carry the allowance on RenderContext, so Envelope lays out over what it is given.

- verifier: Evidence exact: `static constexpr float kReflectionAllowanceSec = 0.010f;` at line 73; prepare() at 302-304 subtracts it from every Envelope's layout window unconditionally, so every envelope (amp, timbre, modulation) lays out 10 ms short and the value cannot vary per instrument. Consequence follows as stated. Context the finding omits: the header comment (68-72) documents this as an intentional, spec'd corpus-wide shift (Note-contained sound, 2026-08-13), the constant is also consumed by mforce_ui/main.cpp:4852 and engine_tests/main.cpp:1041 (which encodes the 10 ms offset in its expectation), and gate_release's re-layout (110) uses refSec without subtracting the allowance, so the two layout paths already disagree on it. Whether the allowance belongs on the instrument/RenderContext is a design question, not a defect; changing it moves every render. Low.
- verifier: Evidence exact: `static constexpr float kReflectionAllowanceSec = 0.010f;` at 73; used at 302-303 (`allowFrames`, `layoutFrames = max(0, frames - allowFrames)`), and `duration` at 304 derives from layoutFrames, so every Envelope in every patch lays out 10 ms short and the value is a class constant that cannot vary per instrument. The consequence as stated follows. Context the finding omits: the header comment (68-72) documents this as an intentional corpus-wide shift after a corpus null test, and tools/engine_tests/main.cpp:1040-1046 already bakes the constant into a test expectation; also note gate_release's re-layout (109-117) does NOT subtract the allowance, so the two layouts are inconsistent. Architectural placement complaint with no measured cost; low.
- judge: Cost is theoretical: 10 ms off a layout window shortens percent stages ~2% on a 0.5 s note, and the comment at envelope.h:68-73 records this as a deliberate, null-tested, corpus-wide decision (2026-08-13 spec); no patch asks for a different allowance. Half the recommendation is infeasible as written — the instrument only prepares the root source (instrument.h:578) and envelopes are prepared by graph propagation, so 'the instrument passes the window into prepare()' has no call site; the RenderContext-carried allowance variant is concrete. Re-opening a settled design is a Matt question, not a review fix.

## F020 [CONFIRMED] low (reporter: low) soundness — engine/include/mforce/core/envelope.h:113

**gate_release re-layout diverges from prepare()'s stage math (no stage_accuracy jitter, no nominal override) and leaves a running stage's end stale**

Reporters: core

Evidence: `109-117: `float stgDur = refSec * stages_[i].percent * timeScale_; stgDur = std::clamp(...); stageCounts_[i] = int(std::lround(...));` vs prepare 332-340 which also applies `if (gated_ && nominal > 0) stgDur = nominal;` and `stgDur *= dist(rng_)`. 119-123: when `currStage_ >= relStage` counts are rewritten but `stageEnd_` for the current stage is not.`

Why: The comment at line 92 promises 'same percent/minSec/maxSec math'; the in-line path (instrument.h:668 passes line_.lastDurSamples) therefore produces a release whose length differs from the prepared one on any patch with stage_accuracy < 1, and the remaining-frames estimate returned to the caller can be off by the stale stageEnd_.

Recommendation: Factor a single `int layout_stage_(int i, float refSec) const` used by both prepare() and gate_release(), and refresh stageEnd_ when the current stage's count changes.

- verifier: Divergence confirmed by reading both paths: gate_release 113-116 computes refSec*percent*timeScale_ then the minSec/maxSec clamp; prepare 327-344 additionally applies `if (gated_ && nominal > 0) stgDur = nominal` (332-333) and the stage_accuracy draw (337-340), and resolves against layoutFrames (frames minus the reflection allowance) whereas gate_release uses the raw releaseRefFrames. The comment at 86-94 does say 'same percent/minSec/maxSec math' (literally it only promises those three terms). Stale stageEnd_ confirmed: 119-123 returns rem from the pre-rewrite stageEnd_ after 111-117 rewrote stageCounts_[currStage_]. One consequence is misstated: because next_raw_ (405-421) also advances on that same stageEnd_, the returned rem matches what actually plays; the real effect is that the running stage's re-laid count is ignored, not that the estimate is wrong. Call path verified: instrument.h:667-668 (finish_line) calls gate_release(line_.lastDurSamples) on every envelope of a held line, and those envelopes were set gated at instrument.h:576, so the nominal omission would bite there. Practical reach today is small: no patch under patches/library or patches/baselines sets `nominal`, and none sets stage_accuracy below 1 as a literal (viola_default.json drives stage_accuracy from a CurveNode via dynamicPins, so a sub-1 value is possible at runtime there). Worth a single layout helper, but low until nominal is used.
- verifier: Confirmed. gate_release 109-117 computes `refSec * percent * timeScale_` then the minSec/maxSec clamp; prepare 327-344 additionally applies `if (gated_ && nominal > 0) stgDur = nominal` (332-333) and `stgDur *= dist(rng_)` when stage_accuracy < 1 (337-340), and resolves percent against layoutFrames = frames - 10 ms allowance (302-304) whereas gate_release uses the raw releaseRefFrames - a third divergence (engine_tests/main.cpp:1027-1046 encodes this asymmetry in its expectations). 119-123: when currStage_ >= relStage, stageCounts_[currStage_] may have been rewritten but stageEnd_ is not refreshed; next_raw_ 423-425 then computes pos against the NEW count while the stage still ends at the OLD stageEnd_, so the ramp position jumps and the returned rem is stale. Caller confirmed: instrument.h:668 `e->gate_release(line_.lastDurSamples)` in finish_line, reached only for held (phrased) lines; the live UI path (mforce_ui/main.cpp:4851) passes the default -1. Practical reach today is small: no patch under patches/library or patches/baselines sets `nominal`, and only strings/viola_default.json drives stage_accuracy (via a curve dynamicPin). The comment at 92 literally promises only 'same percent/minSec/maxSec math', which the code does honor, so the 'promise' framing is slightly over-read, but the divergence and stale stageEnd_ are real. Low, as filed.
- judge: Facts hold but nothing reaches it today: the in-line path (instrument.h:668) only diverges with stage_accuracy<1 or a gated release-stage nominal>0, and no patch in patches/ sets either. The stale-stageEnd_ branch is sharper than the finding states — line 423-424 computes pos from the REWRITTEN count while the stage still ends at the old stageEnd_, so the ramp truncates or overshoots (click) — but it needs a no-expand envelope still mid-final-stage when the line ends (minSec > first-note length), which is rare. The shared layout_stage_ helper resolves the math drift; for the stale state, either skip rewriting stages at or before currStage_ or refresh stageEnd_ from the unchanged stageStart_. The helper should keep a nominal stage's absolute length on re-layout, consistent with absolute_time stages.

## F021 [CONFIRMED] medium (reporter: low) modern-cpp — engine/include/mforce/core/envelope.h:190

**replace_stages() move-assigns a polymorphic object onto itself (`*this = std::move(fresh)`), relying on implicit copy/move of a virtual class and discarding prepared runtime state**

Reporters: core

Evidence: `void replace_stages(Envelope&& fresh) { fresh.stage_accuracy = stage_accuracy; ... *this = std::move(fresh); }  — Envelope/ValueSource declare no copy/move control (C.67); the assignment also resets absolute_time, totalFrames_, ptr_ and stageCounts_ (empty -> next_raw_ at 391 returns 0 until the next prepare).`

Why: Each preset knob change (envelope_presets.h:72,131,190,250,316,394) silently resets the envelope's prepared cursor; the whitelist of fields to carry across (182-189) has already needed a bug-fix extension (minValue/maxValue, per the comment) and will need another for every new field.

Recommendation: Make the stage layout a value member (e.g. `StageLayout layout_`) that rebuild() swaps, keep runtime state untouched, and `= delete` copy/move on ValueSource so slicing assignment cannot recur.

- verifier: Verified: replace_stages (181-191) ends with `*this = std::move(fresh);`; Envelope declares no copy/move/dtor and ValueSource (dsp_value_source.h:75-76) has only `virtual ~ValueSource() = default;` with no copy/move control, so the implicit Envelope move-assignment (and ValueSource's deprecated implicit copy-assign for the base) is what runs. The whitelist at 182-189 and the comment at 173-180 confirm minValue/maxValue was a bug-fix extension. State loss confirmed: fresh carries ptr_=-1, totalFrames_=0, empty stageCounts_ and its own absolute_time (false from make_adsr / plain Envelope), so after a rebuild next_raw_ at 391 returns 0 until the next prepare. All six rebuild() call sites (envelope_presets.h:72,131,190,250,316,394) verified. Reachability on a prepared, running envelope is concrete in the UI stream path: play_continuous (mforce_ui/main.cpp:4860-4885, 4943-4946) calls stream_envelopes_hold() (sets env->absolute_time=true, appends a hold stage) then prepare_graph() on the same s_nodes dspSource objects the audio thread pulls; a Settings-row edit (9517-9523) then calls node->dspSource->set_setting under the audio mutex with no re-prepare (mark_graph_dirty at 791-794 only flips flags; prepare_graph is called nowhere else). So a mid-stream knob change on a preset envelope zeroes its output until the stream is restarted and discards the stream's absolute_time override and hold stage; the UI comment at 4244-4247 acknowledges the wholesale rebuild. The live-keyboard path is unaffected (voices are separate clones rebuilt via the instrument cache). 'Slicing' is loosely worded (base operator= on a final subclass leaves derived fields and the vptr intact), but the mechanism and cost are real. Upgrading to medium: reachable UX defect plus a whitelist that has already needed one fix.
- verifier: Confirmed. envelope.h:190 `*this = std::move(fresh);` inside replace_stages; ValueSource (dsp_value_source.h:75-76) declares only `virtual ~ValueSource() = default` and no copy/move control, so the assignment uses the implicitly-generated operators. `fresh` comes from a factory or a bare `Envelope env(sr_)` and is never prepared, so the assignment installs an empty stageCounts_, ptr_ = -1, totalFrames_ = 0 and the factory's absolute_time (false for make_adsr), none of which are in the 182-189 whitelist; next_raw_ at 391 then returns 0 until the next prepare(). All six cited rebuild sites exist (envelope_presets.h:72,131,190,250,316,394) and every preset knob routes through rebuild() (e.g. ADSREnvelope::set_setting 356-365). On the offline instrument path this is benign: push bindings run before prepare (instrument.h:571-579, 603-607) and isSetting bindings are skipped on line continuation (486-487). But a concrete reachable cost exists in mforce_ui: play_continuous prepares the editor graph's own node objects for a 2 h stream after stream_envelopes_hold() flips each Envelope to absolute_time = true (main.cpp:4255-4271, 4876-4878, 4943-4945); a preset-envelope knob edit mid-stream calls node->dspSource->set_setting under the audio mutex (9407-9411, 9516-9522) -> rebuild -> replace_stages, which empties stageCounts_ and reverts absolute_time, so that envelope outputs minValue (0) for the rest of the stream and nothing re-prepares it (no g_graphEditCounter consumer touches the stream). Traced from code, not executed. That is a live-tweak defect against the mission's 'tweak parameter, hear the results' pillar, hence medium rather than the filed low. The `= delete` recommendation would break the by-value factories and replace_stages as written.
- judge: The C.67 half is theoretical — ValueSource has no data members (dsp_value_source.h:75-131) and the vptr is untouched, so the sliced move-assign clobbers nothing — but the prepared-state reset has concrete, already-paid cost: it caused the TriPower collapse the comment records, deliver_continuation (instrument.h:482-487) skips isSetting bindings specifically because set_setting rebuilds mid-render, and the UI's continuous stream pulls the graph's dspSources directly (main.cpp:3890, 4878) while the properties panel calls set_setting under the audio mutex (9410) and already special-cases env inline vs shape settings (9398-9405) — a preset shape knob mid-stream empties stageCounts_ and the envelope emits 0 until the next prepare. Note-setup bindings are safe (apply before prepare at 572→578). The StageLayout value-member fix resolves it; drop the '= delete copy/move on ValueSource' part — it would break the by-value factory idiom used at envelope_presets.h:67/386, main.cpp:411 and source_registrations.cpp:209 unless Envelope re-defaults them.

## F022 [CONFIRMED] low (reporter: low) soundness — engine/include/mforce/core/envelope_presets.h:23

**Preset envelopes cannot select RampType::Hold: label table has four entries and setters clamp to [0,3]**

Reporters: core

Evidence: `inline constexpr const char* kRampCurveLabels[] = { "Linear", "Expo", "InverseExpo", "Sine", nullptr };  and e.g. line 49 `attackCurve_ = std::clamp(int(value), 0, 3);` while ramp.h:9 defines `enum class RampType { Linear, Expo, InverseExpo, Sine, Hold };` and the generic stage table (tools/mforce_ui/main.cpp:10044) and loader (patch_loader.cpp:693) accept "Hold".`

Why: A valid ramp type is reachable in stage-list envelopes but unreachable in preset ones; the comment at 21-22 predates Hold, which indicates an oversight rather than a decision.

Recommendation: Add "Hold" to kRampCurveLabels, raise the clamp/max to 4, and derive both from a single `kRampTypeCount` constant next to the enum.

- verifier: Verified: envelope_presets.h:23-25 kRampCurveLabels has exactly four labels + nullptr; every xxxCurve setter clamps to [0,3] (lines 49,51,104,164,168,224,227,286,289,292,358,361,365) and every descriptor max is 3.0f. ramp.h:9 defines RampType{Linear,Expo,InverseExpo,Sine,Hold}, with ramp.h:6-8 dating Hold to 2026-09-05; the last commit touching envelope_presets.h is fa2b7df (09-01), so the presets predate Hold. Stage-list loader accepts "Hold" at engine/src/patch_loader.cpp:693 (note: path is engine/src/, not engine/src/core/ as cited) and the UI stage table typeNames includes "Hold" at tools/mforce_ui/main.cpp:10043-10044. The UI Shape combo (main.cpp:9598-9602) sizes itself from the label count, so Hold is unreachable from the UI for preset envelopes, and a JSON value of 4 silently clamps to Sine. Consequence follows. Whether this was an oversight vs. a decision is inference, but no comment or commit says Hold was deliberately excluded from presets.

## F023 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/core/envelope_presets.h:29

**Five preset envelope classes hand-mirror the same descriptor/set_setting/get_setting boilerplate and each re-stores the sample rate the base already holds**

Reporters: core

Evidence: `AREnvelope (29-80), ASEnvelope (84-138), ASREnvelope (142-198), ADSEnvelope (202-258), ADREnvelope (262-326), ADSREnvelope (330-404) each repeat `if (name == "attackCurve") { attackCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }` style chains three times (descriptor row, setter, getter); each declares `int sr_;` (74,133,192,252,318,396) because Envelope::sampleRate_ (528) is private.`

Why: ~330 of 404 lines are mechanical repetition; adding one knob (as attackCurve/attackPower were in 2026-08-29) means editing 15 places, and a missed edit is silent (a descriptor without a setter, or vice versa). The duplicated `sr_` is a second source of truth for a value the base owns.

Recommendation: One PresetEnvelope base holding `std::span<const Knob>` where Knob = {name, SettingType, default, min, max, labels, float PresetEnvelope::*}; generic set/get walk the table and call a virtual rebuild(). Expose `int sample_rate() const` on Envelope and drop the six `sr_` copies.

- verifier: Verified all six class ranges (AR 29-80, AS 84-138, ASR 142-198, ADS 202-258, ADR 262-326, ADSR 330-404) and the six `int sr_;` members at 74,133,192,252,318,396. envelope.h has a single `private:` at 506 and `int sampleRate_;` at 528 with no public accessor (grep for sample_rate() finds nothing), so the sr_ copies are forced by the base's access control. Each class repeats name→field in descriptor/setter/getter; Envelope::set_setting (envelope.h:205-224) silently ignores unknown names, so a descriptor without a matching setter would indeed be a silent dead knob. Minor inaccuracies: title says 'Five' but six classes are listed; '15 places' is approximate. The duplicated sr_ cannot actually diverge in practice (both come from the same ctor arg, and replace_stages at envelope.h:181 moves in an Envelope built from sr_), so 'second source of truth' is structurally true but has no runtime consequence. dsp/BACKLOG.md:123-132 item 36(b) already scopes this cleanup. Concrete maintenance cost, no runtime cost; substantially overlaps F024/F025.

## F024 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/core/envelope_presets.h:29

**Six envelope preset classes repeat ~70 lines of per-stage Curve/Power settings dispatch; seven identical factory lambdas**

Reporters: arch-duplication

Evidence: `ADSR 29-80, ASR 84-138, ADR 142-198, AR 202-258, AS 262-326, ADS 330-404 each hand-list `attackCurve/attackPower/decayCurve/...` in setting_descriptors, set_setting and get_setting; source_registrations.cpp:207-254 registers seven envelope types with the same `[](int sr, auto) { return std::make_shared<X>(sr); }` body.`

Why: Adding a stage knob (the Shape table in the UI properties pane at main.cpp:9560-9624 already has to parse these names by suffix) means editing six classes in three places each.

Recommendation: One `PresetEnvelope` driven by a constexpr table of stage names {"attack","decay",...}; descriptors/set/get generated from the table; the registry registers the table entries in a loop.

- verifier: Material claim verified, but the evidence is sloppy: the class-to-range mapping is wrong (29-80 is AREnvelope not ADSR; 84-138 AS not ASR; 142-198 ASR not ADR; 202-258 ADS not AR; 262-326 ADR not AS; 330-404 ADSR not ADS). Counting Curve/Power lines across descriptor+setter+getter in all six classes gives ~78 lines, so '~70' is fair. source_registrations.cpp:207-254 does register seven envelope types with near-identical factories, but the quoted lambda body is paraphrased — the real body is `auto e = std::make_shared<X>(sr); if (seed) e->set_seed(*seed); return e;`, and the plain 'Envelope' entry differs (make_ar), so it is six identical plus one similar, not 'seven identical'. UI Shape table suffix parsing at main.cpp:9562-9575 confirmed. Consequence (adding a stage knob touches up to six classes × three places) follows. This is the same duplication as F023/F025 and should be folded into F025, whose line cites are accurate.

## F025 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/core/envelope_presets.h:29

**Six copy-pasted envelope preset classes (backlog 36b) plus three envelope authoring dialects (36a) still present**

Reporters: arch-hacks-census

Evidence: `envelope_presets.h:29 `struct AREnvelope final : Envelope`, :84 ASEnvelope, :142 ASREnvelope, :202 ADSEnvelope, :262 ADREnvelope, :330 ADSREnvelope — each repeats the same `set_setting` if-chain (`if (name == "attack") { attack_ = value; rebuild(); return; }` at :48, :103, :163, :223, :285, :357) and `get_setting` chain. Dialects: stage-list form patch_loader.cpp:680-712, preset form envelope_json.h:17-57 (`p.value("preset", std::string("ar"))`), and the registry preset classes above.`

Why: ~380 lines whose only variation is the stage table; adding a per-stage knob (e.g. 'decayCurve') means editing up to six places, and the generic Envelope's own `sustainLevel` setting path (adsrLayout_ at envelope.h:539) is a seventh spelling of the same rewrite. dsp/BACKLOG.md:123-132 already scopes (a) converting preset-form params to explicit stages at load and (b) de-duplicating these classes; neither has moved since 2026-08-29.

Recommendation: Replace the six classes with one table-driven PresetEnvelope (a static array of {name, stage layout, settings} per preset) whose set_setting writes a field in a settings struct and calls one rebuild; convert 'preset' JSON to stages at load so the engine has one envelope representation.

- verifier: Every cite checks out: struct declarations at :29 AR, :84 AS, :142 ASR, :202 ADS, :262 ADR, :330 ADSR; the `if (name == "attack") { attack_ = value; rebuild(); return; }` line at :48, :103, :163, :223, :285, :357 exactly. Three dialects confirmed: stage-list parsing at engine/src/patch_loader.cpp:680-711, preset form in envelope_json.h:17-57 with `p.value("preset", std::string("ar"))` at :19, and the registry preset classes. Generic Envelope's sustainLevel rewrite path exists at envelope.h:217-224 with adsrLayout_ declared at :539. dsp/BACKLOG.md item 36 starts at :123 and scopes (a) load-time stage conversion and (b) preset-class dedup, dated Matt 2026-08-29; recent commits to envelope_json.h/patch_loader.cpp (1e9ac03, 252b360, f94e97d, da35fec) are onset/hold work, not dialect consolidation, so 'neither has moved' holds. Classes span 29-404 = 376 lines, matching '~380'. Most accurate of the three duplication findings; F023 and F024 are duplicates of it. Cost is maintenance only (no hot-path or RT-safety impact).

## F026 [UNVERIFIED] low (reporter: low) soundness — engine/include/mforce/core/multi_source.h:46

**MultiSource `delaySamples` mutes the source's first N samples rather than delaying it, prepare over-provisions frames that are never pulled, and index/null accesses are unchecked**

Reporters: core

Evidence: `prepare: `e.source->prepare(ctx, frames + e.delaySamples);` next: `float v = e.source->next(); if (ptr_ >= e.delaySamples) sum += v * e.weight;` — the source advances every output sample so output[k] = source[k] for k >= delay (a gate), not source[k-delay]. `set_weight(int index, float w) { entries[index].weight = w; }` unchecked. No patch sets a MultiSource `"delay"` (grep over patches/ finds none; loader branch at source_registrations.cpp:365-366 is dead).`

Why: The feature is misnamed relative to what it does and its loader hook is live, so the first patch to use it gets a gate, not a delay. The extra `+ delaySamples` in prepare lengthens envelope layouts inside that source for samples that never render.

Recommendation: Either implement a real delay (do not pull the source until ptr_ >= delay, prepare it with `frames - delay`) or remove delaySamples and the configurator branch. Add null/range checks or assert on add()/set_weight().


## F027 [CONFIRMED] high (reporter: high) soundness — engine/include/mforce/core/ramp.h:55

**Sine ramp with nonzero power (pseudo-sine) is mathematically broken: pow of a negative base for t>0.5 (NaN) and a stray absolute +0.5 offset**

Reporters: core

Evidence: `: endVal - range * std::pow(1.0f - t * 2.0f, power) * 0.5f + 0.5f;  — for t in (0.5,1) the base 1-2t is negative, so std::pow with non-integer power returns NaN; the +0.5f is added to the value, not scaled by range. Legacy MForce/Utility/Ramp.cs:66 has the identical expression, so this is a faithful port of an original defect.`

Why: Reachable from the UI without hand-editing JSON: every preset envelope defaults releaseCurve to Sine (envelope_presets.h:39,154,276,348) and exposes releasePower 0..10. Any nonzero power yields NaN on a rising stage (NaN then poisons every filter state downstream — corrupt audio for the rest of the note) and, on a falling stage, an overshoot below 0 that never lands on endVal (e.g. 0.7->0 with p=2 ends at -0.2). At t=0.5 the two halves are discontinuous by range/2+0.5.

Recommendation: Mirror the first half correctly: second half = endVal - range * pow((1 - t) * 2, power) * 0.5 (no +0.5); descending case mirrored the same way. Add an engine_tests case asserting continuity at t=0.5 and endpoints for power in {0.5, 2, 3}.

- verifier: Evidence confirmed at ramp.h:55 (inside cited 51-59): `endVal - range * std::pow(1.0f - t * 2.0f, power) * 0.5f + 0.5f`. For t in (0.5,1) the base 1-2t is negative; std::pow(negative, non-integer) is a domain error -> NaN. The `+ 0.5f` is an absolute offset, not scaled by range. Discontinuity at t=0.5 on the rising branch computes to range/2 + 0.5 as claimed (left limit startVal+range/2, right limit endVal+0.5). Falling-branch example checked: 0.7->0, p=2 at t=1 gives 0.7 + (-0.7)*pow(2,2)*0.5 + 0.5 = -0.2, never lands on endVal. Additional defect the finding omits: the descending first half (line 58) evaluates to the MIDPOINT at t->0+ (endVal - range*0.5), not startVal, so a release with any nonzero power jumps to mid-level on entry. Legacy confirmed: C:/@dev/repos/mforce-legacy/MForce/Utility/Ramp.cs line 66 has the structurally identical expression (EndValue + (StartValue-EndValue)*Pow(1 - 2*pos, Power)/2 + 0.5f). UI reachability confirmed: envelope_presets.h:39,154,276,348 default releaseCurve to 3.0 = Sine (kRampCurveLabels index 3, RampType enum order Linear,Expo,InverseExpo,Sine); releasePower/attackPower/decayPower are Float 0..10 settings; tools/mforce_ui/main.cpp:9418-9514 renders settings generically (Combo for enum, InputFloat with step (max-min)*0.01 = 0.1 for power, so a single click yields a non-integer power). Stage-form loader patch_loader.cpp:686-692 passes power unclamped and maps "Sine". Ramp::value is called per sample from Envelope::next_raw_ (envelope.h:425) with no clamp/scrub; the only NaN guards in the engine are RefSource guard=true taps (dsp_value_source.h:173) and the partials.h cutoff sentinel, so NaN/overshoot flows straight into whatever the envelope drives. Two precision caveats, not refutations: (1) 'any nonzero power yields NaN on a rising stage' is overstated - integer powers (1,2,3...) give finite-but-wrong values (pow(negative, integer) is defined); non-integer powers NaN. (2) The default-Sine stage is the RELEASE, which is falling (sustain->0), so the NaN path requires a rising Sine stage (attackCurve set to Sine, or a stage-form rising Sine) plus non-integer power; the finding's own text does distinguish rising=NaN vs falling=overshoot. Practical reach today: no patch in patches/library or patches/baselines has a Sine stage with nonzero power (17 files use Sine, all power 0 -> true-cosine path), so no shipped sound is affected; the defect is latent but one slider nudge away in every envelope preset and the generic stage editor. No engine test directory exists for Ramp (only third_party rtaudio/rtmidi tests found). Severity: high - real correctness defect in a core per-sample primitive, UI-exposed on every preset, produces NaN or a full-amplitude jump then hard cut at stage end; not critical because no library patch triggers it and it is a faithful legacy port rather than a regression.
- verifier: Evidence verified verbatim at engine/include/mforce/core/ramp.h:51-59. Math checked by hand: rising branch second half uses pow(1 - 2t, p) (base in (-1,0] for t>0.5) plus an absolute +0.5f; non-integer p gives NaN, integer p gives finite-but-wrong values (p=2: lands at endVal - range/2 + 0.5, jump of range/2 + 0.5 at t=0.5, exactly as claimed). Falling branch 0.7->0, p=2 evaluates to 0.7 - 0.7*pow(2,2)*0.5 + 0.5 = -0.2 as claimed; additionally the falling first half returns endVal - range/2 (the midpoint) as t->0+, so a Sine release with nonzero power also jumps immediately to half-level - a further defect the finding did not list. Legacy ../mforce-legacy/MForce/Utility/Ramp.cs:66 has the identical expression (faithful port confirmed). Reachability confirmed: envelope_presets.h:39,154,276,348 all default releaseCurve to 3 (Sine) and expose releasePower Float 0..10; attackCurve is a 0..3 combo so Sine on a rising stage is one dropdown away; presets are registered in engine/src/source_registrations.cpp:214-251, in the UI add menu (tools/mforce_ui/main.cpp:10856-10859), and settings are rendered generically via ImGui::InputFloat with step 0.1 (main.cpp:9509-9513) so non-integer powers are trivially reachable. Evaluation path is envelope.h:424-425 ramp.value(pos) with no isfinite/NaN guard anywhere in next_raw_; the gate re-anchor at 429-433 derives its shape from the already-corrupt value, so key-up does not mask it. curve.h:41 is a second consumer constructing Ramp{y0,y1,seg.type,seg.power}, so the curve-morph path is affected too. Minor overstatement only: 'any nonzero power yields NaN' is true only for non-integer powers; integer powers yield finite garbage. Mitigating for severity, not truth: grep of patches/library and patches/baselines finds zero patches with a nonzero *Power, so no shipped render is currently corrupted; and tools/engine_tests/main.cpp:561-570 tests Ramp only with Hold, Expo p=0, Sine p=0, so the broken branch has no coverage (recommendation's premise holds). Severity high: UI-exposed control on the default curve produces NaN/overshooting envelopes in a core primitive; not critical because no tracked patch exercises it today.
- judge: Corrupt-audio path reachable from a stock UI slider: every preset envelope defaults releaseCurve to Sine (envelope_presets.h:39/154/276/348, releaseCurve_{3}) and exposes releasePower/attackPower 0..10, so one nonzero slider move puts this branch in the per-sample render loop. Rising stage + non-integer power = NaN from pow(negative base) that poisons downstream filter state for the rest of the note; descending stage (the default release) is worse than the finding states — the first half ALSO starts at the midpoint, not startVal (0.7->0.35 jump at t=0+), then jumps to 0.85 at t=0.5 and overshoots to -0.2 at t=1 (negative gain = polarity flip). Clicks on every release and NaN silence from a visible control would embarrass a release; power defaulting to 0 (library patches unaffected) is the only mitigation, which is why this is critical rather than an active regression. Legacy-faithful is not a defense: no CLAUDE.md rule preserves legacy defects and the no-back-compat memory says just fix the formula. Recommendation resolves it but is loosely worded for the descending case; the better fix is to drop the startVal<endVal branch entirely for the pseudo-sine and use the direction-independent ease-in-out pair `t<0.5 ? startVal + range*pow(2t,p)*0.5 : endVal - range*pow(2(1-t),p)*0.5` (sign of range handles direction; continuous at t=0.5 = midpoint, lands on startVal/endVal at the ends). Test should assert t->0+, t=0.5, t=1 in BOTH directions for p in {0.5, 2, 3}, plus a no-NaN sweep.

## F028 [CONFIRMED] medium (reporter: low) soundness — engine/include/mforce/core/randomizer.h:73

**int_range can return max+1 (and select_int then reads past the vector) when uniform_real_distribution<float> yields exactly 1.0; select_int on an empty vector is UB**

Reporters: core

Evidence: `int int_range(int min, int max) { return min + int(value() * float(max - min + 1)); }  select_int: `return values[int_range(0, int(values.size()) - 1)];` and weighted form ends with `return values.back();`. The comment at 43 documents value() as `[0,1]` inclusive; float uniform_real_distribution can produce the upper bound (LWG 2524).`

Why: Used by 12 music headers (composer, figures, chord_walker, pattern_library...). An out-of-range index is a silent read of adjacent memory in release builds — a rare, non-reproducible composition glitch.

Recommendation: `return std::min(max, min + int(...));` and assert/early-return on empty inputs.

- verifier: Evidence verified verbatim at engine/include/mforce/core/randomizer.h:73-75 (int_range), 86-88 (unweighted select_int), 91-99 (weighted select_int ending in values.back()), comment at line 43 documents value() as [0,1]. Consequence verified against the toolchain actually in use: build/CMakeCache.txt says Visual Studio 17 2022, toolset MSVC 14.41.34120. In that toolset's <random>, uniform_real_distribution<float> goes through _Nrand_impl -> generate_canonical<float,24>(mt19937), which computes float(draw)/float(2^32) with a single 32-bit draw (_Kx=1) and NO clamp. float(0xFFFFFFFF) rounds to 2^32 (confirmed numerically: every draw >= 2^32-128 rounds to 2^32), so value() returns exactly 1.0f with probability ~2^-25 (~3e-8) per draw. Then int_range(min,max) = min + int(1.0f * (max-min+1)) = max+1. Reachable out-of-bounds reads exist at direct-index callers: composer.h:67 pulses[int_range(0,6)], default_strategies.h:180/191/199 opts[int_range(0,N)], chord_walker.h:180, pattern_library.h:193, period_passage_strategy.h:196, library_passage_strategy.h:149, passage_strategies.h:119, random_voicing_profile_selector.h:60/64, plus select_int itself. Caveat on the secondary 'empty vector is UB' part: all 11 select_int call sites (figures.h, figure_transforms.h:548, library_passage_strategy.h:212, passage_strategies.h:672) pass literal non-empty initializer lists, so that branch is latent only, not reachable today. Not on an audio-callback path (all callers are comp-lane music headers), so no RT-safety angle. Recommended std::min clamp is correct and one line.

## F029 [CONFIRMED] medium (reporter: low) smell — engine/include/mforce/core/range_source.h:19

**RangeSource has three defaults for `normalized` and they disagree: ctor default true, member initializer false, descriptor false**

Reporters: core

Evidence: `range_source.h:19 `bool varNormalized = true)` vs :99 `bool varNormalized_{false};  // matches loader absent-key default + descriptor` vs :81 `{"normalized", SettingType::Bool, 0.0f, ...}`. tools/mforce_ui/main.cpp:434-437 constructs with three args and so gets true.`

Why: The member initializer is dead (the ctor always overwrites it) and the ctor default contradicts the documented absent-key contract the comment at 77-80 was written to enforce; a UI-built RangeSource behaves differently from the same node loaded from JSON.

Recommendation: Drop the ctor default (or make it false) and keep one source of truth, the descriptor row.

- verifier: Evidence verified verbatim: range_source.h:19 ctor default `bool varNormalized = true`, :81 descriptor default 0.0f (false) with the comment at 77-80 explicitly tying it to the loader's absent-key contract, :99 member initializer `varNormalized_{false}` which is dead because the only ctor unconditionally assigns it. Loader confirmed: engine/src/patch_loader.cpp:669 `p.value("normalized", false)` -> explicit false for absent key. Consequence confirmed by tracing the UI path: tools/mforce_ui/main.cpp:434-437 builds RangeSource with 3 args (-> true); GraphNode ctor (main.cpp:369-374) runs create_dsp then init_config, which caches the descriptor default (false) into settingValues but does NOT call apply_config; create_source_at_menu (main.cpp:10744-10758) then calls update_node_dsp, which only calls apply_config for NT_ENVELOPE (main.cpp:978-980). So a menu-placed RangeSource runs with varNormalized_=true while its Properties panel shows normalized=false, and the same node after save/reload (UI load path main.cpp:1887-1914 does apply_config; CLI loader gives false) runs with false. The registry factory at engine/src/source_registrations.cpp:188-191 also passes an explicit `true`, a fourth disagreeing default the finding did not list. Audible effect only when var is fed a [-1,1] signal (the remap at :35-38 is skipped). Not a hot-path or CLAUDE.md-rule issue; fix is trivial (make ctor default/registry factory false, or call apply_config after create_dsp).

## F030 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/core/sequence.h:48

**sequence.h is dead (included nowhere in engine/ or tools/) and does not include the headers it uses**

Reporters: core

Evidence: `std::vector<std::unique_ptr<Sequence>> sequences;  with only `#include <vector>` and `<cmath>`; line 25 uses std::move without <utility>. Grep for `sequence.h`, `SimpleSequence`, `CompositeSequence` across *.h/*.cpp/*.txt finds only this file. Also SimpleSequence::get_next (33-37) ignores maxLoops that has_more (28) honors.`

Why: Unreferenced legacy port carrying its own inconsistencies; it compiles only because nothing includes it.

Recommendation: Delete it (git keeps it) or, if the comp lane wants it, add the includes, fix the maxLoops asymmetry and give it a test.


## F031 [CONFIRMED] medium (reporter: medium) efficiency — engine/include/mforce/core/smoothness_interpolator.h:21

**interpolate() evaluates the cosine branch unconditionally, wasting a std::cos per sample for smoothness 0, 0.5 and <0.5**

Reporters: core

Evidence: `float sinVal = sineInterp(v1, v2, pos);  (line 21) computed before the `if (smoothness == 0.0f) return v1;` / `== 0.5f return lerp` branches. Per-sample callers: engine/src/red_noise_source.cpp:109 `interp_.interpolate(lastValue_, nextValue_, pos)` with default `interp_(0.0f, false)`; engine/include/mforce/source/segment_source.h:197; and curve.h:40 for CurveNode at fixed smoothness 0.5.`

Why: RedNoiseSource (Matt's core generator) at its default smoothness pays a libm cos on every sample and discards it; CurveNode's lerp path does the same whenever its input changes. The project's own fast_math.h documents that CRT transcendental calls dominate hot-loop cost.

Recommendation: Move the sineInterp computation into the two branches that use it (== 1.0f and > 0.5f).

- verifier: Evidence verified at engine/include/mforce/core/smoothness_interpolator.h:21: `float sinVal = sineInterp(v1, v2, pos);` (std::cos inside) runs before the smoothness branches, and sinVal is consumed only in the `== 1.0f` and `> 0.5f` (else) branches; the 0.0, 0.5 and <0.5 branches discard it. Per-sample callers confirmed: engine/src/red_noise_source.cpp:109 (inside compute_wave_value, called per sample), engine/include/mforce/source/segment_source.h:197 (next()), curve.h:40 via Curve::eval_seg reached per sample from ShaperSource::next/map (shaper_source.h:187-216) and CurveNode (curve_node.h:93 passes fixed 0.5f). One factual correction: the finding's 'RedNoiseSource at its default smoothness (interp_(0.0f,false))' is wrong — the constructor sets smoothness_ = ConstantSource(1.0f) (red_noise_source.cpp:11) and compute_wave_value copies smoothness_->current() into interp_ every sample (line 39), so the effective RedNoise default is 1.0, where sinVal IS used; the baseline RedNoise patches mostly use 0.7-0.8 (also use sinVal), one uses 0.5 (wasted). The waste is real at the 0.5 defaults of SegmentSource (descriptor default 0.5), ShaperSource (ConstantSource(0.5f), shaper_source.h:20) and CurveNode (fixed 0.5), and for any patch with smoothness <= 0.5. Build is MSVC /O2 with default /fp:precise (no /fp:fast anywhere in CMake), under which MSVC does not drop the unused cos call (it is not treated as side-effect-free), so the per-sample cost is not optimized away — modest uncertainty here, not verified by disassembly. Consequence follows: a discarded transcendental per sample on hot paths, and fast_math.h's header documents measured libm cost (~40% of additive loop in sinf). Not a CLAUDE.md rule violation (no heap/lock/unbounded work), pure efficiency; fix is a trivial move into the two consuming branches.

## F032 [CONFIRMED] low (reporter: low) soundness — engine/include/mforce/core/var_source.h:19

**VarSource relative mode carries lastVal_ across prepare(), so a re-prepared voice continues from the previous note's last value**

Reporters: core

Evidence: `void prepare(...) { val_->prepare(ctx, frames); var_->prepare(ctx, frames); varPct_->prepare(ctx, frames); }  — lastVal_ (94) is never reset; next() uses it at 33-34: `float delta = val_->current() - lastVal_; cur_ = lastVal_ + delta * (...)`.`

Why: Latent today (no patch sets "absolute": false), but the voice pool re-prepares cached instances per note, so the first relative-mode patch will get note-to-note state leakage.

Recommendation: Reset lastVal_ in prepare() (to 0 or to val_'s initial value).

- verifier: Evidence verified at the cited lines: var_source.h:19-23 prepare() only forwards to val_/var_/varPct_ and never touches lastVal_ (declared line 94, default 0); next() lines 33-34 compute delta from lastVal_ in relative mode and line 37 overwrites lastVal_ every sample. Consequence verified: the engine re-prepares the same cached voice graph per note (instrument.h:578 and :607 call vg.source->prepare(ctx, durSamples) on the pool voice for every note; main.cpp:3752-3754 documents the live play path doing prepare + schedule on pool-backed voices under g_audioMutex), so in relative mode the first sample of note N+1 is computed from the last sample of note N. Latent status also verified: patch_loader.cpp:659 defaults absolute to true, the only patch that sets the key is patches/baselines/MPXTest3.json:34 with true, and the UI constructor (main.cpp:422-425) uses the default; the only route to relative mode today is the UI 'absolute' setting toggle (var_source.h:79-81). Absolute mode is unaffected (cur_ fully recomputed). Severity low: no shipping patch hits it, fix is a one-line reset in prepare().

## F033 [CONFIRMED] medium (reporter: medium) efficiency — engine/include/mforce/core/var_source.h:26

**Every pin costs two virtual calls per sample (next() then current()) although next() already returns the value; constants pay the same price**

Reporters: arch-valuesource-graph

Evidence: `val_->next(); var_->next(); varPct_->next(); if (absolute_) { cur_ = val_->current() * (1.0f + var_->current() * varPct_->current()); }

range_source.h:31-40: min_->next(); max_->next(); var_->next(); float v = var_->current(); ... cur_ = min_->current() + v * (max_->current() - min_->current());
svf_source.h:99-101: float in = source_ ? (source_->next(), source_->current()) : 0.0f; float fc = cutoffFreq_ ? (cutoffFreq_->next(), cutoffFreq_->current()) : 1000.0f;
partials.h:557-564 + 793-797: multEnv_->next(); amplEnv_->next(); ... (8 calls)  then  sMultE_ = multEnv_->current(); sAmplE_ = amplEnv_->current(); ... (8 more)
ks_string.h:223-224, 373: frequency_->next(); amplitude_->next(); ... float ampl = amplitude_ ? amplitude_->current() : 1.0f;`

Why: The idiom is uniform across the codebase (confirmed in VarSource, RangeSource, CombinedSource, SVFSource, KSString, DelayLineSource, Envelope, Partials): the return value of next() is discarded and current() is re-dispatched. That doubles the indirect-call count on every edge of the graph, per sample. A Partials node with eight ConstantSource envelope pins spends 16 virtual calls per sample reading eight numbers that never change; KSString spends 10 reading its 5 pins. perform_source_design.md §1.6 acknowledges 'the engine already pays that pull price on every constant pin of every patch' — this is that price, doubled, and it is pure overhead with no behavioural reason (RefSource::next() and ::current() are identical; PerformOut likewise). On the live path the audio callback holds a mutex while doing this for every voice.

Recommendation: Short term: use the return value (`const float v = val_->next();`) everywhere — a mechanical change, bit-identical. Medium term: introduce a `Pin` helper (shared_ptr<ValueSource> + cached float + `float pull(tick)`), which removes the boilerplate (see the elegance finding), gives one dispatch per edge, and lets ConstantSource be detected once at wire time (`isConst`) so constant pins are a load from the Pin, not a call. Measure with the existing additive ladder (tools/ablate_layers.py) before and after.

- verifier: All quoted evidence confirmed: var_source.h:26-31 (three next() calls, return values discarded, then three current() calls); range_source.h:30-40 (cited 31-40, one line off); filter/svf_source.h:99-101 comma-expression idiom; source/additive/partials.h:557-564 eight next() calls then refresh_sample_scalars() at 793-801 re-dispatching current() on the same eight pins; source/ks_string.h:223-224 and :373. RefSource::next()/current() are identical (dsp_value_source.h:163-164 both call read()), PerformOut likewise (perform_source.h:120-121), ConstantSource::next() just copies v_ to cur_ (dsp_value_source.h:136). perform_source_design.md:78 contains the quoted 'already pays that pull price on every constant pin' line. Audio callback confirmed: main.cpp:3864 takes g_audioMutex and :3890 pulls voice.source->next() per voice per sample under it. All pins are shared_ptr<ValueSource> so no devirtualization is possible; the per-edge dispatch count is genuinely 2x (and 4x through a RefSource wrapper, since each of its calls forwards to source->current()). Caveats that keep this medium rather than high: the cost is unmeasured and the finding's own 'doubles' is per edge, not per sample total; a Partials node's per-partial loop or a delay-line read dwarfs 8 extra ~ns indirect calls per sample, so the real-world fraction is probably low single-digit percent at most. The short-term recommendation (use next()'s return value) is bit-identical only where next() returns exactly what current() then returns, which holds for every source checked here but was not exhaustively verified across all ~40 ValueSource types. Not a CLAUDE.md rule violation (no allocation, no locks added, bounded work); it is a legitimate efficiency item worth a mechanical pass plus a measurement.

## F034 [CONFIRMED] low (reporter: low) smell — engine/include/mforce/filter/biquad_source.h:104

**Resonance mode overwrites the a1/a2 *settings*, so get_setting reports derived coefficients and a mode flip back to Raw inherits them**

Reporters: filters

Evidence: `a1_ = -2.0f * r * std::cos(2.0f * pi * f / float(sampleRate_));
a2_ = r * r;
... get_setting: if (name == "a1") return a1_; if (name == "a2") return a2_;`

Why: a1_/a2_ are user-facing settings (setting_descriptors 51-52) and also the live resonance-mode state. After any resonance-mode render, a patch save via get_setting persists the last computed cos/r² values instead of the authored ones, and switching mode to Raw keeps filtering with them until the user retypes a1/a2. Minor because resonance patches rarely flip modes, but it is state aliasing of a settings field.

Recommendation: Keep the derived pair in separate members (e.g. ra1_, ra2_) selected in next(), leaving the b/a settings untouched by rendering.

- verifier: Evidence confirmed verbatim: biquad_source.h:104-106 assign a1_/a2_ from the frequency/radius pins in resonance mode, and get_setting (lines 69-70) returns those same members; prepare() (86-94) does not restore them and set_setting("mode") (57) flips only the flag. Consequence 1 (mode flip to Raw keeps the derived a1/a2) HOLDS only for the UI continuous-stream path: main.cpp:3939-3940 renders s_nodes' live dspSource objects directly, and the inspector's render_setting_row (main.cpp:9516-9523) calls set_setting for the edited name only, so after a Resonance render a Raw flip filters with the last cos/r^2 pair until a1/a2 are retyped or the graph is reloaded. Consequence 2 (patch save via get_setting persists derived coefficients) is REFUTED: the save path (main.cpp:3072 serialize) writes node.settingValues, the UI-side authored cache, not get_setting; settingValues is re-pulled from get_setting only at load (1935) and paste (10380), before any render, or for a per-note paramMap target by name (4572). The engine has no get_setting-based serializer (only patch_loader.cpp:460 reads 'hysteresis'). All other render paths (CLI, live voice pool get_cached_instrument 4629-4647, generate_unified 4347-4352) go serialize->load and get the authored JSON values, so the stale state is confined to the in-place editor object in stream mode. Net: real but narrow state aliasing; one of two stated consequences follows.

## F035 [UNVERIFIED] medium (reporter: medium) smell — engine/include/mforce/filter/filters.h:13

**Two biquad kernels in one unit with opposite a/b naming; the BW one is a generic shift-register FIR+IIR with a redundant, bounds-unsafe `n` parameter**

Reporters: filters

Evidence: `filters.h:18 `float process(float input, const float* a, int n)` shifts `vals` (a std::vector) with two loops and `value += a[i] * vals[i]`; BWLPSection stores feedforward in `float a[3]` and feedback in `float b[2]` (lines 57, 68-73) and runs `iir.process(fir.process(gain * input, a, 3), b, 2)`; BiquadSource (biquad_source.h:114) is the textbook form `y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2`.`

Why: Readability cost: a/b mean feedforward/feedback in biquad_source.h and the reverse in filters.h, in the same directory. Performance cost: per section per sample the BW path does two O(n) shift loops over heap-backed vectors plus two indirections versus five MACs and four scalar moves; a 2-section BW node is four such sections. Safety cost: `n` duplicates `vals.size()` and nothing checks n <= order, so a future caller passing n > order reads past the vector. FIRFilter/IIRFilter have no consumers outside the BW sections (grep), and fm_source.h:176 consumes BWLPSection only for decimation.

Recommendation: Express a BW section as a plain 5-coefficient struct with the same DF-I/TDF-II kernel and naming as BiquadSource (or literally reuse a shared `BiquadKernel` struct that BiquadSource also wraps), and delete FIRFilter/IIRFilter.


## F036 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/filter/filters.h:54

**Butterworth LP and HP sections and filters are copy-pasted pairs differing only in section type**

Reporters: arch-duplication, arch-modern-cpp

Evidence: `BWLPSection 54-80 vs BWHPSection 85-111; BWLowpassFilter 116-189 vs BWHighpassFilter 194-264 (~75 lines each: descriptors, set/get_param, prepare, next, cutoff recompute).`

Why: ~100 duplicated lines; a fix to section recomputation or the `cutoffFreq` smoothing must be made twice.

Recommendation: `template <class Section> class BWFilter` with `using BWLowpassFilter = BWFilter<BWLPSection>;` and the type name as a template parameter/static member.

Also reported as: Butterworth LP/HP sections and filters are near-identical copies; BiquadState + ap1_phase_delay duplicated between KSString and AllpassResonator


## F037 [UNVERIFIED] low (reporter: low) modern-cpp — engine/include/mforce/filter/filters.h:63

**39 hand-typed PI literals instead of std::numbers**

Reporters: arch-modern-cpp

Evidence: `filters.h:63 `std::cos(3.14159265358979 * (2.0 * k + n - 1.0) / (2.0 * n))`, also 67, 94, 98; mixer.cpp:41-42; svf_source.h:106, 119, 155; biquad_source.h:104, 130; envelope.h:522; hybrid_ks_source.cpp:7; basic_additive_source.h:135; tools/mforce_ui/main.cpp:3966-3967, 13383; stk_ref/mesh2d_ref.cpp:181 (39 total)`

Why: Mixed precisions (`3.14159265358979` double in filters.h vs `3.14159265358979323846f` elsewhere) and local `constexpr float PI` redefinitions; C++20 provides std::numbers::pi_v<float>.

Recommendation: Replace with std::numbers::pi_v<float>/<double> (one include of <numbers>); keep TAU as a single constexpr in fast_math.h.


## F038 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/filter/filters.h:116

**BWLowpassFilter/BWHighpassFilter and BWLPSection/BWHPSection are copy-pasted pairs; FIR/IIR use heap vectors with shift loops for fixed 2nd-order sections**

Reporters: arch-build-headers

Evidence: `filters.h:116-189 `struct BWLowpassFilter final : ValueSource {` and :194-264 `struct BWHighpassFilter final : ValueSource {` are identical line-for-line except `BWLPSection`/`BWHPSection` and the type_name string (compare :161-178 with :239-253); sections :54-80 and :85-111 share constructor, zeta, `process()` and differ only in `update()`. FIRFilter :18-21 `for (int i = n - 1; i > 0; --i) vals[i] = vals[i - 1];` over a `std::vector<float> vals` of size 3; IIRFilter :43-45 same over size 2.`

Why: ~150 duplicated lines in a header compiled into every consumer; a fix to one filter (e.g. the cutoff-caching optimisation at :166-172, which was added to both by hand) must be mirrored. The generic vector-shift FIR/IIR costs two indirections and a loop per sample where a fixed biquad is four multiply-adds.

Recommendation: `template <class Section> struct BWFilter : ValueSource` instantiated as `using BWLowpassFilter = BWFilter<BWLPSection>;` (type_name via a static constexpr in the section); make sections hold `std::array<float,3>`/`std::array<float,2>` state and a direct-form-II transposed `process()`.


## F039 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/filter/filters.h:117

**Dead parallel accessor API (set_source/get_source/set_cutoffFreq/...) duplicated on every node beside set_param/get_param, with zero callers**

Reporters: filters

Evidence: `filters.h:117-120, 195-198, 270-275, 358-366; limiter.h:24-30; reverb.h:21-31; vibrato.h:35-36 — e.g. `void set_source(std::shared_ptr<ValueSource> s) { source_ = std::move(s); }` immediately followed by `set_param("source", ...)` doing the same thing.`

Why: Grep across engine/ and tools/ finds no call to any of these on a filter node (the only set_frequency/get_frequency hits are on RedNoiseSource/WaveSource/FormantSource). Two ways to wire the same pin means two places to keep in sync and ~45 lines of noise; BiquadSource, SVFSource and HammerBank already omit them and are the cleaner model.

Recommendation: Delete the typed accessors on BWLowpass/BWHighpass/BWBandpass/DelayFilter/Limiter/Reverb/Vibrato; set_param/get_param are the registry-driven path and the only one used.


## F040 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/filter/filters.h:194

**BWLowpassFilter, BWHighpassFilter (and BWBandpassFilter) are copy-pasted; BWLPSection/BWHPSection likewise differ only in update()**

Reporters: filters

Evidence: `filters.h:116-189 BWLowpassFilter and 194-264 BWHighpassFilter are identical line-for-line except `type_name()` and `std::vector<BWLPSection>` vs `std::vector<BWHPSection>`; BWLPSection (54-80) and BWHPSection (85-111) share constructor, zeta, fir/iir members and process() and differ only in the body of update(); BWBandpassFilter (269-352) repeats the descriptor/set_param/get_param/prepare/next scaffold a third time.`

Why: ~150 duplicated lines in one header; any fix (e.g. the guard convention, a null-check, a descriptor hint) must be applied three times and has already been applied unevenly — only the LP copy carries the 2026-08-12 audit comment (166-168). The registry (source_registrations.cpp:417-434) already instantiates them with identical lambdas apart from the type.

Recommendation: One `template <class Section> struct BWFilter final : ValueSource` parameterised on the section type and a `static constexpr const char* kName`, registered under the three names; one `BWSection` struct with `enum class Kind { Lowpass, Highpass }` selecting the update() formula (or two free functions computing coefficients into the same struct). Bandpass becomes two BWFilter instances in series or a thin wrapper over two section vectors.


## F041 [CONFIRMED] high (reporter: high) soundness — engine/include/mforce/filter/filters.h:427

**DelayFilter never clamps delayTime; a delay longer than the 1 s buffer indexes buffer_[negative]**

Reporters: filters

Evidence: `float delaySamples = dt * float(sampleRate_);
float readPos = float(ptr_) - delaySamples;
if (readPos < 0.0f) readPos += float(buffer_.size());
int s1 = int(readPos) % int(buffer_.size());
...
float delayVal = (buffer_[s1] + (buffer_[s2] - buffer_[s1]) * frac) * dl;`

Why: dt comes from delayTime_->current() with no clamp; the descriptor max (1.0 s) is a UI hint only — resolve_param (patch_loader.cpp:105) wires any node, so an envelope/range/LFO can exceed it. With dt = 1.5 s at 48 kHz and ptr_ = 100: readPos = 100 - 72000 + 48000 = -23900; C++ truncating % keeps it negative; buffer_[-23900] is an out-of-bounds read inside the per-sample render path (UB, garbage audio or a crash). Every sibling node in the unit clamps its modulated params (BW cutoff at 164/242/326, SVF at 102-103, Limiter at 85-86, Reverb at 102-105); DelayFilter is the one omission.

Recommendation: Clamp once per sample: delaySamples = std::clamp(dt * float(sampleRate_), 1.0f, float(buffer_.size() - 2)); then the single '+= size' wrap is sufficient and s1/s2 are always in range (also removes the need for the two % operations on s1/s2).

- verifier: Evidence verified verbatim at engine/include/mforce/filter/filters.h:427-434 (next() at 414-442). dt = delayTime_->current() at line 422 is used unclamped; the only wrap is a single `if (readPos < 0) readPos += size` at 429, then `int(readPos) % int(size)` at 431. Traced the arithmetic: with buffer_.size()==sampleRate (ctor line 403, 1 s), dt=1.5, sr=48000, ptr_=100 -> readPos = 100-72000+48000 = -23900; C++ truncating % leaves -23900; buffer_[-23900] and buffer_[-23899] are OOB reads. General condition: OOB whenever delaySamples > ptr_ + size, i.e. any dt > 1.0 s hits at ptr_=0 and dt in (1,2) hits a fraction of every buffer cycle. Clamp absence confirmed: `delayTime` appears nowhere else in engine/ or tools/ with a clamp; patch_loader.cpp resolve_param (line 105-137) wraps numbers as ConstantSource and refs as-is with no range check (its only clamps are stage_accuracy/ramp_accuracy at 705/707), and dsp_value_source.h:32-33 states descriptor ranges are advisory with no enforcement, so even a constant 2.0 in JSON triggers it, not just modulators. Envelope (envelope.h:277-278, minValue/maxValue +/-100000) and Range (range_source.h:55-56) can trivially exceed 1.0. Reachability: DelayFilter is registered (source_registrations.cpp:446) and in the UI add-node menu (tools/mforce_ui/main.cpp:10870); audio_callback (main.cpp:3857) pulls voice.source->next() per sample at 3890, so a DelayFilter in a live patch runs this read on the RtAudio thread. Sibling-clamp comparison confirmed at the cited lines: filters.h 164/242/326-327, svf_source.h 102-103, limiter.h 85 (clamp) and 86 (std::max floor), reverb.h 102-105; the one existing user (patches/baselines/delay_test.json, delayTime 0.15 constant) is unaffected. Recommendation's math checks: clamping delaySamples to [1, size-2] guarantees -size < readPos <= size-2 so one += size wrap suffices and s2 = s1+1 <= size-1 without the % ops. Severity high rather than critical: real OOB/UB on the audio thread (~95 KB before the heap block at the example value, garbage audio or crash), but no library patch exercises it today and it needs an out-of-descriptor-range delayTime.
- verifier: Evidence verified verbatim at filters.h:427-434 (dt read unclamped at 422, delaySamples at 427, single '+= size' wrap at 429, truncating % at 431-432, indexed read at 434). Arithmetic checks: with dt=1.5 s, sr=48000, ptr_=100 -> readPos = 100-72000+48000 = -23900; int(-23900) % 48000 == -23900 in C++; buffer_[-23900] is an OOB read. Generally any dt > 1.0 produces a negative index on the samples where ptr_ < (dt-1)*sr, i.e. once per buffer cycle. Consequence chain confirmed: patch_loader.cpp:105 resolve_param wires any node with no range check and engine/src contains zero references to max_value, so neither refs NOR constants are clamped to the descriptor (a plain "delayTime": 1.5 in JSON also triggers it from the CLI, which is broader than the finding states); Envelope's maxValue descriptor allows +/-100000 (envelope.h:278), so a modulator can emit arbitrarily large dt (float->int conversion of int(readPos) then becomes UB as well). Reachability: DelayFilter is registered in source_registrations.cpp:446 and creatable in the UI menu (main.cpp:10870); the RtAudio audio_callback (main.cpp:3857) pulls voice.source->next() per sample at 3890, so this OOB sits on the live render path. Sibling-clamp citations verified: filters.h 164/242/326-327, svf_source.h 102-103, reverb.h 102-105, limiter.h 85 (86 is a std::max lower bound only). Minor overstatement: biquad_source.h:101 reads frequency_/radius_ unclamped in resonance mode, so 'every sibling clamps' is not literally true, but biquad has no array indexing so the OOB point stands. Mitigating context for severity: no patches/library/ patch uses DelayFilter; the only user is patches/baselines/delay_test.json with a safe constant 0.15, so nothing shipped triggers it today. High, not critical: real UB/crash on the audio thread from a trivially-authored patch, but latent rather than release-embarrassing.
- judge: Verified in filters.h:427-434: no clamp, truncating % keeps a negative int(readPos) negative, so any delayTime > 1 s (modulator, or a hand-typed constant — resolve_param at patch_loader.cpp:105 wraps numbers in ConstantSource without checking descriptor range) is a per-sample OOB read in the audio-thread render path, i.e. crash/garbage-audio class UB. Held at high rather than critical because no tracked patch can trigger it today: the only DelayFilter user is patches/baselines/delay_test.json at 0.15 s (the library winds 'Delay' entries are groups containing DelayLineSource, not DelayFilter); upgrade to critical the moment a modulated-delayTime patch enters library/. The clamp to [1, size-2] is the right fix, but the recommendation's claim that the % on s2 can then go is wrong — with ptr_=100, delaySamples=100.5 the wrapped readPos is size-0.5, s1=size-1, s2=size; keep a wrap on s2 (conditional or %). A NaN dt also slips through std::clamp; a `if (!(x >= lo)) x = lo` style guard or std::isfinite check closes that.

## F042 [CONFIRMED] low (reporter: low) modern-cpp — engine/include/mforce/filter/hammer_bank.h:128

**Hand-written pi literals of differing precision instead of C++20 std::numbers::pi_v<float>**

Reporters: filters

Evidence: `hammer_bank.h:128 `2.0f * std::sin(3.14159265f * fc / float(sampleRate_))`; biquad_source.h:104,130 and svf_source.h:106,119,155 `3.14159265358979323846f`; filters.h:63,67,94,98 `3.14159265358979` (double); mixer.cpp:41-42 inline literal.`

Why: The project targets C++20 and nothing in the repo includes <numbers> (grep: zero hits; 30 literal occurrences across 20 engine files). Within this unit alone three spellings coexist, one of them 9 digits (hammer_bank) next to 21-digit neighbours — a per-file constant that should be one named value.

Recommendation: `#include <numbers>` and use `std::numbers::pi_v<float>` / `std::numbers::pi` (or a single `mforce::kPi` in core if a float/double pair is wanted); drop the local `constexpr float pi` copies in biquad_source.h:130 and svf_source.h:155.

- verifier: Evidence verified exactly: hammer_bank.h:128 has `2.0f * std::sin(3.14159265f * fc / float(sampleRate_))`; biquad_source.h:104,130, svf_source.h:106,119,155, filters.h:63,67,94,98 (double literal), mixer.cpp:41-42 all present as quoted. CMakeLists.txt sets CMAKE_CXX_STANDARD 20; grep for `<numbers>` across engine/ and tools/ returns zero hits. Literal count is actually larger than stated (48 occurrences in 29 files under engine/+tools/, vs the finding's 30/20), which only strengthens the duplication claim. Caveat on the 'differing precision' angle: 3.14159265f and 3.14159265358979323846f round to the identical IEEE float (float carries ~7 significant digits), so there is no numeric consequence in hammer_bank.h; the only real effect is the double literal in filters.h, which is used in double arithmetic and is correct. The finding is an accurate observation of inconsistency with no concrete runtime, correctness or RT-safety cost; it does not contradict any CLAUDE.md rule. Pure style/maintainability nit.

## F043 [REFUTED] low (reporter: low) efficiency — engine/include/mforce/filter/limiter.h:90

**Per-sample transcendental recomputation and non-const/double-evaluated helpers in sample paths**

Reporters: arch-modern-cpp

Evidence: `limiter.h:90 `float releaseCoef = std::exp(-1.0f / std::max(1.0f, relSec * float(sampleRate_)));` inside the per-sample loop; shaper_source.h:206 `float map(float x) {` is non-const though it only reads; pulse_source.h:54-59 and basic_additive_source.h:97-111 evaluate the same sub-expression twice per sample`

Why: std::exp per sample in a limiter that sits on every voice output is avoidable cost; a non-const map() prevents calling it from const contexts (the UI already works around this).

Recommendation: Cache releaseCoef when relSec changes (compare-and-update), make map() const, hoist the repeated sub-expressions.

- verifier: Evidence partially confirmed, stated consequences refuted. CONFIRMED: limiter.h:90 has `std::exp(-1.0f / std::max(1.0f, relSec * float(sampleRate_)))` inside next() (per-sample). shaper_source.h:206 `float map(float x) {` is non-const (actual path is engine/include/mforce/source/shaper_source.h, not generator/). basic_additive_source.h:97-111 (engine/include/mforce/source/additive/) does duplicate `freqVarSpeed_->next()/float(sampleRate_)` then `->current()/float(sampleRate_)`; pulse_source.h:54-59 is a discarded next() result re-read via current() (comment says it is a deliberate legacy GetNext() pattern), not really a repeated sub-expression. REFUTED: (1) 'a limiter that sits on every voice output' is false — Limiter is a user-placed patch-graph node (header comment line 17, registered in source_registrations.cpp:452, menu item main.cpp:10872); grep shows zero uses in patches/library/, one baseline test patch (_fx_limiter_test.json), and no instrument/engine code instantiates it. The thing on every output is render/limiter.h::soft_clip, a different function with no exp. So the per-sample exp costs only patches that explicitly contain a Limiter — one exp per sample per instance, not a voice-wide tax. (2) 'the UI already works around this [non-const map()]' is unsupported — every UI caller (main.cpp:6543, 9699) constructs a fresh non-const `ShaperSource probe;` local and calls probe.map() directly; there is no const_cast, no const ShaperSource anywhere in tools/ or engine/. Also note release_ is a modulatable ValueSource, so 'cache when relSec changes' is a compare-and-branch per sample, a micro-opt. Net: a legitimate low nit (hoist/cache the exp, mark map() const) with two fabricated justifications; no concrete measured cost, no RT-safety issue (exp is bounded, no alloc).

## F044 [UNVERIFIED] low (reporter: low) performance — engine/include/mforce/filter/reverb.h:118

**Ring-buffer wrap via integer modulo per sample (12 idivs/sample in Reverb, 3 in DelayFilter, 1 in Limiter)**

Reporters: filters

Evidence: `reverb.h:118 `c.idx = (c.idx + 1) % int(c.buffer.size());` and :128 `a.idx = (a.idx + 1) % int(a.buffer.size());` (8 combs + 4 allpass); filters.h:415 `ptr_ = (ptr_ + 1) % int(buffer_.size());`, :431-432 `int s1 = int(readPos) % ...; int s2 = (s1 + 1) % ...;`; limiter.h:105 `int read = (write_ + 1) % lookaheadSamples_;``

Why: Integer division is ~20-40 cycles and the divisor is a runtime value (vector size), so it cannot be strength-reduced; Reverb pays ~12 of them per sample, roughly the cost of the filtering itself. The index is known to be in [0, size) so a compare-and-reset is exact.

Recommendation: `if (++c.idx == size) c.idx = 0;` (store size as an int member once at construction); in DelayFilter, after the clamp from the OOB finding, s1 is in range and s2 needs only `if (s2 == size) s2 = 0`.


## F045 [CONFIRMED] low (reporter: low) modern-cpp — engine/include/mforce/filter/svf_source.h:30

**Unscoped enums and non-explicit single-argument constructors**

Reporters: arch-modern-cpp

Evidence: `svf_source.h:30 `enum Mode { kLowpass = 0, kHighpass = 1, kBandpass = 2, kLowpass1P = 3, kHighpass1P = 4 };`; partials.h:246 `enum PartialsRngLayer : uint32_t {`; biquad_source.h:20 `BiquadSource(int sampleRate)`; envelope_presets.h:30, 85, 143, 203, 263, 331 `AREnvelope(int sampleRate) : Envelope(sampleRate) ...`; partials.h:267 `Partials(uint32_t seed = 0xADD2'0000u)`, 1073, 1176, 1260`

Why: Unscoped enums leak enumerators and convert to int implicitly (svf mode is already set from a float at test line 300); non-explicit ctors allow `std::shared_ptr<Partials> p = 48000;`-style accidental conversions and int-to-node conversions in overload resolution.

Recommendation: Use `enum class` (the rest of the codebase already does) and mark single-argument constructors `explicit`.

- verifier: Evidence confirmed at every cited location: svf_source.h:30 plain `enum Mode`, partials.h:246 plain `enum PartialsRngLayer : uint32_t`, biquad_source.h:20 `BiquadSource(int sampleRate)` non-explicit, envelope_presets.h:30/85/143/203/263/331 six non-explicit `XEnvelope(int sampleRate)` ctors, partials.h:267/1073/1176/1260 `Partials(uint32_t seed = ...)` and three subclasses non-explicit. 'Rest of codebase uses enum class' is substantially true: 28 `enum class` vs these 2 plain enums under engine/. tools/engine_tests/main.cpp:300 `f.set_setting("mode", float(c.mode))` exists, but that is the SettingDescriptor float interface by design and says nothing about the unscoped enum (an explicit float() cast works identically on enum class), so that piece of 'why' is irrelevant. The `std::shared_ptr<Partials> p = 48000;` example is WRONG C++ — shared_ptr has no converting ctor from T, so that line does not compile either way. A real conversion surface does exist: additive_source2.h:116 `apply_partials(const Partials& p)` would accept a bare integer via the implicit Partials(uint32_t) ctor. Net: constructs exist, consequence holds in weakened form, no observed bug, pure hygiene.

## F046 [CONFIRMED] medium (reporter: medium) soundness — engine/include/mforce/filter/svf_source.h:32

**Sample rate is baked into nodes at construction while RenderContext also carries it; a host-driven sample-rate change cannot propagate and nodes disagree on which to trust**

Reporters: arch-valuesource-graph

Evidence: `svf_source.h:32: explicit SVFSource(int sampleRate) : sampleRate_(sampleRate) {}
ks_string.h:58-59: explicit KSString(int sampleRate) : sampleRate_(sampleRate) {
dsp_wave_source.h:10-11: explicit WaveSource(int sampleRate) : sampleRate_(sampleRate)
delay_line_source.h:19-25: explicit DelayLineSource(int sr) : sampleRate(sr), ... buf_(size_t(sr / 20 + 4), 0.0f) {}
partials.h:417: rate_ = float(ctx.sampleRate);   // reads the context instead
render_context.h:5-6: // Ambient render state propagated top-down through ValueSource::prepare.`

Why: The registry factory signature is `(int sampleRate, optional seed)` so every node freezes the rate at creation; RenderContext::sampleRate is then threaded through every prepare() but most nodes ignore it (Partials is the exception and would run at a different rate from the SVF feeding it if the two ever disagreed). A JUCE host receives the real rate in prepareToPlay after the plugin (and its patch) is constructed; today that would require rebuilding every voice graph. The UI already pins live audio to 48 kHz because of this (main.cpp:3901-3902 'live audio is pinned at 48 kHz (see the rate-bake audit, backlog 69)').

Recommendation: Make RenderContext the single source of truth: nodes read ctx.sampleRate in prepare() and (re)derive coefficients/buffer sizes there, with buffers sized once for a declared maximum rate (or reallocated only in a non-RT `configure(ctx)` call). Drop sampleRate from SourceFactory. Partials already follows this pattern; apply it uniformly and delete the per-node `sampleRate_` members.

- verifier: Evidence confirmed: svf_source.h:32 bakes sampleRate_; ks_string.h:58-59, dsp_wave_source.h:10-11, delay_line_source.h:19-25 (buf_ sized from ctor sr) likewise; partials.h:417 `rate_ = float(ctx.sampleRate)` reads the context; render_context.h:5 comment and `int sampleRate` field at :12; source_registry.h:18-19 `SourceFactory = std::function<shared_ptr<ValueSource>(int sampleRate, std::optional<uint32_t> seed)>` confirms rate is frozen at factory time. Only 4 files consume ctx.sampleRate (partials.h, mesh2d_source.h, wave_evolution.h, full_additive_source.cpp), so 'most nodes ignore it' is accurate and a ctx/ctor disagreement would split the graph. Consequence (host prepareToPlay rate cannot propagate without rebuilding graphs) follows. One loose attribution: 'UI pins 48 kHz because of this' — main.cpp:3901-3902 and BACKLOG.md item 69 attribute the pin to per-sample 48k-baked CONSTANTS (dnAmp 0.9995, ringEnv 0.99958), not to the ctor-baked sampleRate_ member; related but not the same mechanism. Not a violation of any CLAUDE.md rule; concrete future cost under the stated JUCE-compatibility direction, no present-day breakage since everything runs at 48k.

## F047 [CONFIRMED] medium (reporter: medium) performance — engine/include/mforce/filter/svf_source.h:119

**Per-sample transcendental recompute of slowly-varying coefficients in three nodes, contradicting the unit's own tan()-guard convention**

Reporters: filters

Evidence: `svf_source.h:119 (every sample in 1P modes): const float g1 = std::tan(3.14159265358979323846f * lastFc_ / float(sampleRate_));
limiter.h:90 (every sample): float releaseCoef = std::exp(-1.0f / std::max(1.0f, relSec * float(sampleRate_)));
mixer.cpp:41-42 (every sample, every channel): float aL = kRoot2 * std::cos(t * 0.5f * pi); float aR = kRoot2 * std::sin(...);`

Why: filters.h:166-168 records that per-sample tan() made each BW node ~10x costlier than its filtering (2026-08-12 audit) and svf_source.h:25-27 claims 'tan() only re-runs when cutoff or resonance actually move' — but the 1P branch computes tan from lastFc_ every sample even though lastFc_ only changes inside the guarded block two lines above. Limiter pays an exp per sample for a release that is a constant in every patch; the mixer pays cos+sin per sample per channel for a pan that is almost always constant.

Recommendation: SVF: compute a1p_ = g/(1+g) inside the `if (fc != lastFc_ || res != lastRes_)` block and use it in the 1P branch. Limiter: cache releaseCoef keyed on relSec (same guard idiom). Mixer: cache aL/aR keyed on p (or fold into the shared pan helper from the duplication finding).

- verifier: All three sites confirmed verbatim: svf_source.h:119-120 computes `std::tan(pi * lastFc_ / sampleRate_)` on every next() in kLowpass1P/kHighpass1P, although lastFc_ only changes inside the guarded block at :105-114 — directly contradicting the header comment at :25-27 ('tan() only re-runs when cutoff or resonance actually move'). filter/limiter.h:90 computes `std::exp(-1/max(1, relSec*sampleRate_))` every sample with no guard. engine/src/mixer.cpp:41-42 computes cos+sin per sample per channel inside the frames loop. filters.h:166-168 holds the cited 2026-08-12 audit note (per-sample tan made BW nodes ~10x costlier). The recommended fix (fold a1p into the existing guard) is trivially correct since the 1P coefficient depends only on lastFc_. Real per-sample transcendental cost on render paths, scoped to 1P SVF modes, the limiter, and the mixer.

## F048 [CONFIRMED] medium (reporter: medium) efficiency — engine/include/mforce/filter/vibrato.h:97

**Vibrato::prepare() rebuilds its entire LFO sub-graph (~10 make_shared) on every note-on, although the graph depends only on edit-time settings**

Reporters: filters

Evidence: `if (enabled_) {
  build_lfo();           // rebuild from current config scalars
  lfo_->prepare(ctx, frames);
}
... build_lfo(): auto speedRamp = std::make_shared<ASEnvelope>(sampleRate_); ... std::make_shared<RangeSource>(std::make_shared<ConstantSource>(1.0f), std::make_shared<ConstantSource>(speed_), speedRamp, true); ... lfo_ = std::make_shared<RedNoiseSource>(sampleRate_, seed_); lfo_->set_smoothness(std::make_shared<ConstantSource>(1.0f)); ...`

Why: prepare() runs per note from Instrument::prepare_voice_at (instrument.h:578/607), which the UI calls from the keyboard/MIDI handling path (main.cpp:4691, 4801) — not the audio callback, so this is not an RT violation, but it is a burst of ~10 heap allocations (plus shared_ptr control blocks and the old graph's destruction) on every live note in five library patches (oboe1, oboe2, bassoon1, viola_default, cello_full_range — grep patches/library). The settings it reads (speed_, depth_, attack_, speedVar_, depthVar_, zct_, seed_) only change through set_setting at edit time; ASEnvelope lays its ramp out from `frames` in its own prepare(), so re-preparing the existing graph is sufficient.

Recommendation: Build the LFO once in the constructor and mark it dirty in set_setting(); prepare() rebuilds only when dirty, otherwise just forwards prepare to lfo_. Keep the per-note reseed() path as is (it forwards to the existing lfo_).

- verifier: Evidence confirmed at vibrato.h:97-100 and build_lfo() 121-141: 13 make_shared per call (2 ASEnvelope, 2 RangeSource, 8 ConstantSource, 1 RedNoiseSource), and RedNoiseSource's ctor (red_noise_source.cpp:10-15) adds 6 more ConstantSource allocations, so ~19 heap allocations plus destruction of the previous graph per prepare. Call path confirmed: Instrument::prepare_voice_at is at instrument.h:349-373 (vg.source->prepare at 365); the cited 578/607 are the play_note offline paths, not prepare_voice_at, but all three are per-note prepares. UI call sites main.cpp:4691 (play_note) and 4801 (play_note_held) confirmed on the UI thread (keyboard + polled RtMidi getMessage at 5111); not the audio callback, BUT both hold g_audioMutex which audio_callback (3864) also takes, so the allocation burst extends the window the audio thread blocks on (whole-voice prepare is already under that lock by design, so this is incremental cost, not a new violation). Five library patches confirmed via grep. Settings only mutate via set_setting; Envelope::prepare (envelope.h:295+) lays out from frames and reseeds, so the ASEnvelope half of the argument holds. ONE CORRECTION to the recommendation: 're-preparing the existing graph is sufficient' is false as stated. RedNoiseSource::prepare (red_noise_source.cpp:20-28) does not reset rng_ or ramp state (sampleCount_, rampSize_, lastValue_, nextValue_), and reseed() is deliberately NOT called on the fresh-note path (instrument.h:469-471). Today the per-note rebuild is what gives each fresh note a seed-anchored LFO from a clean ramp; a build-once/dirty-flag fix that merely forwards prepare would let the noise stream and ramp state free-run across notes and change rendered output (breaks per-note determinism / byte-identity). A correct fix must also call lfo_->reseed() and reset the ramp state in Vibrato::prepare (or add that reset to RedNoiseSource::prepare). Severity medium: concrete per-note allocation/free burst under the audio mutex, not a per-sample path.

## F049 [CONFIRMED] medium (reporter: medium) soundness — engine/include/mforce/music/basics.h:116

**Uninitialized aggregate members: Pitch{} is a null-pointer sentinel that note_number() dereferences; Chord::def, Section::beats and PoolAtom fields are likewise uninitialized**

Reporters: arch-modern-cpp

Evidence: `116-117: `const PitchDef* pitchDef;` / `int octave;` then 119: `float note_number() const { return float(octave * 12 + pitchDef->offset); }`; piece_utils.h:38,39,46,92 `return Pitch{};`; basics.h:335 `const ChordDef* def;`; structure.h:198 `float beats;` not set by the default ctor at 209 `Section() : scale(Scale::get("C", "Major")) {}`; pool_figure_builder.h:17-20 `int noteCount; float totalBeats; Contour contour; long count;``

Why: A default-constructed Pitch/Chord/Section has indeterminate or null members; any consumer of the piece_utils sentinel that calls note_number() crashes, and a default Section carries garbage beats into timing math.

Recommendation: Add default member initializers (`const PitchDef* pitchDef{nullptr}; int octave{4};`, `float beats{0.0f};`), return std::optional<Pitch> from the lookup helpers instead of a sentinel, and make note_number() precondition-checked.

- verifier: Evidence at basics.h:116-119 verified verbatim. `Pitch{}` is value-initialized (pitchDef=nullptr, octave=0), so for Pitch the right word is 'null sentinel', not 'indeterminate'. Crash path confirmed: piece_utils.h:92 returns Pitch{} when the passage is not yet realized and the template has no startingPitch; AlternatingFigureStrategy (composer.h:1868 -> runningReader.set_pitch at 1872) and PhraseAwareFigureStrategy (composer.h:1974 -> set_pitch at 1976) take that value unguarded into PitchReader::set_pitch, which dereferences p.pitchDef at pitch_reader.h:40 (and note_number() at :37). DefaultPassageStrategy alone guards this (composer.h:1327-1337 throws by name) and its comment records that a real template (template_shaped_test.json) hit the no-startingPitch case, so the input is realistic. The secondary claims are latent only: Chord::def is assigned at every construction site (chord.cpp:228/242/274, music_json.h:256 via create; composer.h:687 `Chord chord;` is immediately overwritten by resolve); Section's default ctor is used only by music_json.h:603 which sets beats at :557, and composer.h:341 uses the 5-arg ctor; PoolAtom (pool_figure_builder.h:17-20) has every field assigned at :36-39 before use. No concrete 'garbage beats into timing math' path exists. Claim holds on the Pitch sentinel; the rest is hygiene.

## F050 [CONFIRMED] low (reporter: medium) smell — engine/include/mforce/music/basics.h:280

**Dead legacy API left in the core headers: SimpleNote/Tone/DrumHit/Beat, RhythmicFigure, semitones_between, passing-tone helpers, reader_before, generate_musical_rhythm, and test-only transforms**

Reporters: music-model

Evidence: `struct SimpleNote { ... };   // basics.h:280 — no references outside the header
struct Tone { ... };         // basics.h:289
struct DrumHit { ... };      // basics.h:301
struct Beat { ... };         // basics.h:311`

Why: Grep over the engine and tools finds no uses of: basics.h SimpleNote/Tone/DrumHit/Beat (280-316), Scale::semitones_between (162-174, also has an unused `int d` at 165 and `i % len` goes negative for negative degrees), Scale::get_passing_tone (198-226); figures.h RhythmicFigure (633-644); pitch_reader.h chromatic_step/snap_to_degree/has_passing_tone_up/down (61-79); piece_utils.h reader_before (98-103); rhythm_util.h generate_musical_rhythm (25-68). figure_transforms.h vary/complexify/embellish (351-404) and replicate_and_prune (188-218) are referenced only from tools/test_figures; complexify additionally uses try/catch as control flow (372-378) around a catch that can never fire because addAt is always in range. Each is API surface that must be read, kept compiling and kept consistent (the rest-dropping bug above lives partly in this set).

Recommendation: Delete the unreferenced types/functions; move test-only transforms into tools/test_figures or delete them with their tests; keep the registry of live transforms to what apply() and the strategies call.

- verifier: Lines 280-316 match. Repo-wide grep (engine/ + tools/, .h/.cpp) finds SimpleNote, Tone, DrumHit, Beat only in basics.h; Scale::semitones_between (162-174) and get_passing_tone (198-226) only their definitions; RhythmicFigure only figures.h:633; PitchReader::chromatic_step/snap_to_degree/has_passing_tone_up/down only pitch_reader.h:61-79; reader_before only piece_utils.h:98; generate_musical_rhythm only rhythm_util.h:25 (header is included by shape_strategies.h but the function is never called). vary/complexify/embellish/replicate_and_prune are referenced only from tools/test_figures/main.cpp:366,460,472,488; apply() routes TransformOp::Complexify to elaborate(), not complexify (figure_transforms.h:563-568). try/catch claim confirmed: addAt = int_range(0, note_count()-1), note_count()==units.size() (figures.h:537), and split/add_neighbor/add_turn throw only on repeats<2 or index out of range (figure_transforms.h:226,252,279), so the catch at 376 is unreachable. Unused `int d` at basics.h:165 and sign-preserving `i % len` for negative degrees also confirmed. One caveat for the recommendation: CLAUDE.md near-term priority 1 names RhythmicFigure as an example data class slated for a JSON format, so deleting it may conflict with a stated plan. Severity: dead, unreferenced API surface with no runtime cost - low.

## F051 [CONFIRMED] low (reporter: low) smell — engine/include/mforce/music/basics.h:333

**Chord mixes identity, duration, a performer hint and resolved pitches, and the whole struct rides inside every Element variant**

Reporters: music-model

Evidence: `struct Chord {
  Pitch root;
  const ChordDef* def;
  int inversion{0};
  int spread{0};
  float dur{1.0f};
  std::optional<std::string> figureName;  // performance hint for ChordPerformer

  std::vector<Pitch> pitches;`

Why: Element (structure.h:63) is `std::variant<Note, Chord, Hit, Rest>`, so every Note-only element is sized for a Chord with a vector and an optional<string>; ElementSequence::add has to special-case `chord().dur` (structure.h:90, flagged for removal) because duration lives inside the payload rather than on the Element; ScaleChord::resolve must thread duration/inversion/spread through just to build one (chord.cpp:252-280).

Recommendation: Split into a ChordSpec (root/def/inversion/spread/pin) and keep duration and figure hints on the Element/Note like every other element type; resolve pitches on demand.

- verifier: Facts verified: Chord layout at basics.h:333-341 as quoted; Element is std::variant<Note, Chord, Hit, Rest> at structure.h:63; ElementSequence::add special-cases e.chord().dur at structure.h:90 with the in-code comment 'remove with stage 9'; ScaleChord::resolve(scale, octave, duration, inversion, spread) at chord.cpp:252-280 builds a Chord carrying dur/inversion/spread. The size consequence is true in principle (variant is sized to its largest alternative) but overstated: Note already holds an Articulation variant plus an Ornament variant whose alternatives contain std::vector, so the per-element delta from Chord's vector + optional<string> is modest, ElementSequence is composer-side (not a render path), and no concrete cost is shown. The special-casing is already acknowledged as temporary in the code. Design-preference finding with no measured cost - low.

## F052 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/music/chord_progression_builder.h:52

**Self-declared "throwaway-grade" std::function map of five fixed lambdas is the production path for progressionName**

Reporters: music-builders-voicing

Evidence: `:16 `// Throwaway-grade: hard-coded named lambdas`; :53 `static const std::unordered_map<std::string, Builder> map = { {"I-V7-V7-I", [](float beats) { ... }}` — wired at composer.h:348 `section.chordProgression = ChordProgressionBuilder::build(sd.progressionName, sd.beats);``

Why: Five near-identical lambdas (each `float bar = beats / N; prog.add(...)x4`) behind type-erased std::function for no runtime polymorphism; available() returns hash order so any listing is non-deterministic.

Recommendation: Replace with a constexpr table `{name, {degree,quality}[], divisor}` and a single builder loop; or load named progressions from styles/*.json alongside the StyleTable so authored data and code stop diverging.


## F053 [CONFIRMED] low (reporter: low) soundness — engine/include/mforce/music/chord_walker.h:229

**is_chord_tone ignores alteration, assumes a 7-degree scale, and infers a 7th from intervals.size() > 3**

Reporters: music-builders-voicing

Evidence: `:230-231 `int r = chord.degree % 7; int d = scaleDegree % 7;` (alteration unused); :236-238 `if (chord.quality && chord.quality->intervals.size() > 3) { ... if (d == (r + 6) % 7) return true; }` — melody degrees are wrapped by scale.length() in period_passage_strategy.h:93,97.`

Why: bVII is treated as diatonic vii; a pentatonic scale wraps at 5 but tones are tested mod 7; "6"/"add9" get a phantom 7th and sus chords get a phantom 3rd. Correct only for the diatonic-triad + V7/viio content of the one shipped table.

Recommendation: Derive chord tones from ChordDef intervals resolved against the actual scale (ScaleChord::resolve already does this) and compare pitch classes, or at least use scale.length() and reject alteration != 0.

- verifier: Evidence verified verbatim at chord_walker.h:229-241 (r = chord.degree % 7, d = scaleDegree % 7, alteration never read, 7th inferred from intervals.size() > 3). Consequence verified: (1) ScaleChord.alteration (basics.h:402) is a semitone root shift honored by ScaleChord::resolve (chord.cpp:267) and parsed from 'b'/'#' prefixes by ChordLabel::parse (style_table.h:35-36), so bVII would be tested as diatonic vii; (2) build_melody_profile wraps melody degrees by scale.length() (period_passage_strategy.h:85,93,97) using the section scale, which is JSON-selectable (music_json.h:206) and includes 5-note Major/Minor Pentatonic (music.cpp:58-59), so a pentatonic section would compare mod-5 degrees against mod-7 chord-tone offsets; (3) ChordLabel::parse passes any unrecognized suffix straight to ChordDef::get (style_table.h:79), and the registry has '6' (4 intervals), 'add9' (4), 'sus2'/'sus4' (3) (chord.cpp:17-18,26,33), so an authored style table using them gets a phantom 7th / phantom 3rd. Call sites: harmonize (chord_walker.h:143,150,175) and pick_next (:285), reached from period_passage_strategy.h:375,395 and composer.h:878. Minor inaccuracy in the finding: there are three shipped tables (styles/nursery_v1, classical_tonic_dominant, classical_mozart), not one, but all three contain only diatonic triads + V7 + viio, for which the function is correct. Latent correctness gap, no shipped content currently miscomputed; recommendation (resolve ChordDef intervals against the actual scale and compare pitch classes) is sound.

## F054 [CONFIRMED] medium (reporter: medium) smell — engine/include/mforce/music/classical_composer.h:30

**The IComposer/Genre facade is dead and its narrower overloads cannot work on the event path**

Reporters: music-composer

Evidence: `compose.h:15-51 IComposer and Genre: grep shows no IComposer* / Genre use anywhere in engine/ or tools/ — every caller constructs `ClassicalComposer composer(tmpl.masterSeed)` concretely (main.cpp:570, engine_tests:1877, test_figures x6). classical_composer.h:44-48 forwards to `inner.compose_one_passage(...)` which (composer.h:296-305 -> compose_passage_ :898) writes only `part->passages[sectionName]`; realize_event_sequences_ runs only from compose() (:237) and skips parts whose elementSequence is non-empty (:433). No caller of the 3- or 4-arg overload exists (grep).`

Why: Three virtual overloads, a Genre container and compose_one_passage exist to support a 'compose one passage' API that, after Stage 8, updates a passage tree Conductor no longer reads (conductor.h:560) — on a composed piece the audible output does not change, on a fresh piece there is nothing to play (and the 3-arg overload iterates an empty piece.sections). classical_composer.h:38-43 documents the fresh-piece case as 'undefined'. Abstractions that cannot be exercised are maintenance cost only.

Recommendation: Delete compose.h (IComposer, Genre), classical_composer.h and Composer::compose_one_passage; expose Composer directly. If per-passage regeneration is wanted later, design it on top of a public `realize_events(Piece&)` so the narrow path also refreshes events.

- verifier: Verified every cited point. (1) classical_composer.h:30-48 is as quoted: 3-arg overload loops piece.sections and forwards to the 4-arg, which calls inner.compose_one_passage. (2) Grep over engine/ and tools/ for IComposer / Genre (struct) / compose_one_passage: the only hits are the definitions in compose.h and the facade itself (note_map.h/melody_profile.h 'Genre' hits are prose comments, not the struct). (3) All 9 callers (mforce_cli/main.cpp:571, engine_tests:1515,1878, test_figures x6) construct ClassicalComposer concretely and call the 2-arg compose(piece, tmpl) only; no 3-/4-arg call exists. (4) composer.h:296-305 compose_one_passage -> compose_passage_ (:826-~900) ends with part->passages[sectionName] = move(passage) and nothing else; realize_event_sequences_ (:429) is called only from compose() (:237) and skips any part whose elementSequence is non-empty (:433). (5) conductor.h:560-563 reads only part.elementSequence. So on an already-composed piece the narrow overloads rewrite the passage tree without touching events (inaudible), and on a fresh piece the 3-arg overload iterates empty sections (header :38-43 itself calls that 'undefined'). Minor caveat not affecting the verdict: the passage tree is still read by non-Conductor consumers (mforce_cli:851 for printing, passage_strategies.h:211 prev-section lookup, piece_utils.h), so 'passage tree no longer read' is only true of Conductor, as the finding states. Dead-abstraction/maintenance claim stands; no CLAUDE.md rule contradicted.

## F055 [CONFIRMED] low (reporter: low) smell — engine/include/mforce/music/composer.h:38

**Library header pulls <iostream> and emits unroutable std::cerr diagnostics from the compose path**

Reporters: music-composer

Evidence: `:38 `#include <iostream>`; :250 `std::cerr << "Unknown phrase strategy '" << n << "', falling back to default_phrase\n";`, :260, :637-639, :654-655, :1586; the resolve-with-cerr-fallback block is itself duplicated at :247-252 and :1583-1588.`

Why: Every TU including composer.h (and therefore every strategy header through the hub include list) pays for <iostream>; the UI and batch tools cannot capture or silence these messages; unknown-strategy fallbacks hide authoring errors in a stream nobody checks (the project elsewhere prefers throwing by name).

Recommendation: Replace the unknown-name fallbacks with a thrown std::runtime_error naming the strategy (consistent with realize_motifs), route remaining diagnostics through one small logging hook, and drop <iostream> from the header.

- verifier: Evidence confirmed: `#include <iostream>` at composer.h:38; std::cerr fallbacks at :250 (phrase), :260 (passage), :637-639 (voicingSelector), :654-655 (voicingProfileSelector), :1586 (DefaultPassageStrategy). The resolve-with-cerr-fallback block at :247-252 is textually duplicated at :1583-1588 (same strings, same logic). realize_motifs throws std::runtime_error on an unresolved motif (:137), so the 'project elsewhere throws by name' contrast is real. Weakness: the <iostream> compile-cost consequence is overstated — ten other music headers that composer.h pulls in (passage_strategies.h, phrase_strategies.h, period_passage_strategy.h, wrapper/two_figure/elaborated phrase strategies, smooth_voicing_selector.h, library_passage_strategy.h, pattern_library.h, dun_parser.h) also include <iostream>, so dropping it from composer.h alone saves nothing. The real content is the silent unknown-strategy fallback, which is a latent authoring trap, not a live bug.

## F056 [CONFIRMED] medium (reporter: medium) smell — engine/include/mforce/music/composer.h:164

**Composer's constructor re-registers ~20 strategies/factories into four process-global singletons on every instance construction**

Reporters: arch-build-headers, arch-music-model, music-composer

Evidence: `:164-168 `explicit Composer(uint32_t seed ...) : rng_(seed + 200) { auto& reg = StrategyRegistry::instance(); reg.register_figure(std::make_unique<DefaultFigureStrategy>()); ...` through :222-224 `realReg.register_strategy(...)`; strategy_registry.h:16 `figures_[s->name()] = std::move(s);` silently replaces the previous object.`

Why: ClassicalComposer is constructed per seed inside loops (main.cpp:570 in the --compose batch loop; engine_tests:1877 inside a per-seed lambda; six constructions in test_figures). Each construction destroys and recreates every registered strategy; any `FigureStrategy*` obtained from the registry before a second Composer is built dangles. The registries are unsynchronized, so the natural next step (parallel batch rendering, one Composer per thread) is a data race in the constructor. Instance construction with global side effects is also the opposite of what the 'explicit registries' rule intends.

Recommendation: Move registration to a one-time `register_builtin_strategies()` guarded by a `static const bool once = ...` (or call it from main/CLI startup), leave Composer's constructor to seed the RNG only. Make register_* refuse duplicates (assert or return bool) so a second registration is loud.

Also reported as: Process-global registries are repopulated by every Composer construction and are unsynchronized; the strategy RNG is a thread-local with an implicit Scope precondition | Composer's constructor re-registers ~25 strategies into process-global singletons on every construction, destroying the previous instances

- verifier: Evidence confirmed: Composer ctor at :164-224 registers 3 default + 3 shape figure strategies, 5 phrase strategies, 8 passage strategies, 1 voicing selector, 4 profile-selector factories, 2 realization strategies into four process-global singletons (StrategyRegistry, VoicingSelectorRegistry, VoicingProfileSelectorRegistry, RealizationStrategyRegistry) on every construction. strategy_registry.h:16-18 does `figures_[s->name()] = std::move(s)` — silent replace, previous object destroyed. Call sites confirmed: mforce_cli/main.cpp:570 is inside `for (int i = 0; i < count; ++i)` (line 535); engine_tests:1877 is inside the per-seed `melody` lambda; test_figures has 6 constructions (107,188,746,813,940,1157). Caveats on consequence: no current caller holds a FigureStrategy*/PhraseStrategy* across a second construction (all resolve immediately before use), and there is no parallel composition anywhere today, so the dangling-pointer and data-race consequences are latent, not live. 'Opposite of explicit registries rule' is editorial — the registry IS explicit; the issue is where registration happens. Worth fixing, no measured cost.

## F057 [CONFIRMED] medium (reporter: low) smell — engine/include/mforce/music/composer.h:241

**Dead or unreachable members: Composer::compose_figure/compose_phrase/registry_get_for_phase2, SectionStrategy, duration-keyed ChordPerformer lookup, unused locals/params**

Reporters: music-composer

Evidence: `composer.h:241-254 `MelodicFigure compose_figure(...)` / `Phrase compose_phrase(...)` and :288-290 `registry_get_for_phase2` — no callers (grep; strategies resolve the registry directly). strategy.h:21-30 `class SectionStrategy` — StrategyRegistry has no register_section (strategy_registry.h:16-18). conductor.h:392-402 duration-keyed `figures`/`defaultFigure` — no writer anywhere (grep `chordPerformer.figures`); spec 2026-04-22 lists ChordPerformer/Josie as 'already dead' yet register_josie_figures is still called (main.cpp:469, mforce_ui:8126). conductor.h:550 `const Scale& scale = section.scale;` unused. composer.h:404 `realize_motifs_(const Piece& /*piece*/, ...)` unused parameter. realization_strategy.h:37-43 RealizationRequest has no NSDMIs (`float startBeat;`).`

Why: Dead public members on Composer also expose a trap: compose_figure/compose_passage are public and call paths that hit `::mforce::rng::next()` (:963, :1199), which dereferences a thread_local that is nullptr unless an `rng::Scope` is active (rng.h:8-11); only compose_passage_ and generate_default_passage_ install one (:819, :884). Dead abstractions (SectionStrategy, Josie figures) mislead readers about what the framework supports.

Recommendation: Delete the three Composer members, SectionStrategy, the duration-keyed ChordPerformer path (or the whole ChordPerformer figure library if no patch uses figureName), the unused local/param; give RealizationRequest default member initializers. Make the rng Scope part of the public entry points (compose/compose_one_passage) and keep everything below private.

- verifier: Checked each sub-claim. compose_figure (:241-244), compose_phrase (:246-254), registry_get_for_phase2 (:288-290): grep across engine/ and tools/ finds no callers (all strategies call StrategyRegistry::instance().resolve_* directly). SectionStrategy (strategy.h:21-30): no references anywhere else, no registry slot. conductor.h:392-402 duration-keyed `figures` + `defaultFigure` (:313-314): register_josie_figures writes only `namedFigures`; no code anywhere writes `figures[...]` or assigns defaultFigure, so that path can never hit. Spec docs/superpowers/specs/2026-04-22-composer-owns-event-sequence-design.md:291 does mark ChordPerformer/register_josie_figures '✓ (already dead)' while mforce_cli:469 and mforce_ui:8126 still call it and conductor.h:599 still dispatches perform_chord — the spec row is wrong, the finding's inconsistency observation is accurate. conductor.h:550 `const Scale& scale = section.scale;` — no other use of `scale` in perform() (lines 545-640). composer.h:404 `/*piece*/` unused param confirmed. realization_strategy.h:37-43 RealizationRequest has no NSDMIs confirmed. rng trap confirmed: public compose_figure → DefaultFigureStrategy::compose_figure :1199 `::mforce::rng::next()` → rng.h:11 derefs thread_local `detail::current` that is nullptr unless a Scope is live; only two Scope installs exist (:819, :884), both private. Public compose_passage likewise. No external caller today, so it is a footgun on a public API rather than a live crash. Note recommendation to 'delete the whole ChordPerformer figure library' goes beyond the evidence — ChordPerformer is live for chord events; only the duration-keyed branch is dead.

## F058 [CONFIRMED] low (reporter: medium) smell — engine/include/mforce/music/composer.h:241

**Composer's public dispatch surface is dead or duplicated, and two-thirds of the plan/compose interface is never dispatched**

Reporters: arch-music-model

Evidence: `composer.h:241-244 `MelodicFigure compose_figure(Locus, const FigureTemplate&)` and 246-254 `Phrase compose_phrase(...)` have no callers (strategies resolve the registry directly: 1274, 1584, 1724, 1874, 1967), and DefaultPassageStrategy repeats compose_phrase's body including the cerr fallback at 1583-1588. 288-290 `registry_get_for_phase2` -- no callers. 296-305 `compose_one_passage` is reachable only through ClassicalComposer's 3/4-arg IComposer overloads (classical_composer.h:30-48); every call site is the 2-arg form (test_figures, engine_tests, mforce_cli:571). strategy.h:21-30 `class SectionStrategy` has no registry slot; `plan_figure` (37) and `plan_phrase` (48) are never called -- only plan_passage (composer.h:264).`

Why: Readers (and the 2026-04-20 audit) reason about a two-phase, four-level strategy contract that the dispatcher only honours at one level; the dead facade (IComposer/ComposerRegistry in compose.h) and dead helpers add surface without behaviour and keep the 'ctx.composer' mental model alive.

Recommendation: Delete compose_figure/compose_phrase/registry_get_for_phase2/compose_one_passage, ClassicalComposer, IComposer and SectionStrategy; make DefaultPassageStrategy call one shared `resolve_phrase_or_default()` helper; either dispatch plan_figure/plan_phrase from the default strategies or remove the virtuals so strategy.h describes what runs.

- verifier: Largely a restatement of F057 from a second reporter. Confirmed: compose_figure/compose_phrase/registry_get_for_phase2 have no callers; DefaultPassageStrategy at :1583-1588 repeats compose_phrase's body including the cerr fallback; resolve-registry-directly sites are at :1274, :1584 and near the compose_figure calls at :1747/:1888/:2028 (cited 1724/1874/1967 are a few lines off but the code is there). compose_one_passage (:296-305) is reached only via ClassicalComposer's 3-/4-arg IComposer overloads (classical_composer.h:30-48); every call site in tools (engine_tests:1515,1878; mforce_cli:571; test_figures x6) is the 2-arg form. plan_figure (strategy.h:37) and plan_phrase (:48) are never called anywhere; only plan_passage is dispatched (:264). Inaccuracy: compose.h has IComposer and Genre, not a 'ComposerRegistry'; Genre has no users outside compose.h. Consequence is reader-confusion / dead surface only; no concrete runtime cost, and the one real hazard (rng Scope on public entry points) is already F057's. Dead-code cleanup, not medium.

## F059 [CONFIRMED] low (reporter: low) duplication — engine/include/mforce/music/composer.h:299

**Part/Section lookup-by-name loops are hand-written six times in Composer**

Reporters: music-composer

Evidence: `Find part by name: :299-302 `for (auto& pt : tmpl.parts) { if (pt.name == partName) { partTmpl = &pt; break; } }`, :593-596, :831-834, :855-858; find section by name: :843-845, :852-854, :866-868; plus section_start_beat_ :420-427.`

Why: Each copy is a chance to diverge (two of them return a pointer, two an index, one silently returns 0.0f on miss). Piece/PieceTemplate have no `find_part(name)` / `section_index(name)` helpers so every caller re-rolls them.

Recommendation: Add `Piece::section_index(const std::string&)`, `Piece::find_part(name)`, `PieceTemplate::find_part(name)` / `find_section(name)` returning std::optional/int and use them.

- verifier: All eight sites exist as cited: find-part-by-name :299-302 (ptr), :593-596 (ptr), :831-834 (ptr), :855-858 (index); find-section-by-name :843-845 (ptr), :852-854 (index), :866-868 (SectionTemplate ptr); section_start_beat_ :420-427 (silently returns 0.0f on miss). grep finds no find_part/find_section/section_index helper on Piece, PieceTemplate or piece_utils.h. Divergence claim (pointer vs index vs 0.0f-on-miss) is accurate. Style/duplication nit with no observed behavioral cost.

## F060 [CONFIRMED] low (reporter: low) soundness — engine/include/mforce/music/composer.h:313

**Piece-level scaleName other than Major/Minor is silently collapsed to Major and inherited by every section without an override**

Reporters: music-composer

Evidence: `:313-314 `piece.key = Key::get(tmpl.keyName + " " + (tmpl.scaleName == "Minor" ? "Minor" : "Major"));` and :338-340 `Scale secScale = (!hasSectionKey && sd.scaleOverride.empty()) ? piece.key.scale : Scale::get(secTonic, secType);` (Key supports only KeyType Major/Minor, basics.h:233-245).`

Why: A template with `"scaleName": "Dorian"` composes in Major with no diagnostic. No tracked score uses an exotic piece-level scaleName today (grep), so this is a latent silent default rather than a live bug — but the loader accepts the field.

Recommendation: Resolve the section scale from `Scale::get(tmpl.keyName, tmpl.scaleName)` when scaleName is not Major/Minor (mirroring the keyContext path at :370-374), or reject unknown scaleName at load time.

- verifier: Confirmed: composer.h:313-314 collapses any scaleName other than 'Minor' to 'Major' when building piece.key; :338-340 gives sections with no keyName and no scaleOverride `piece.key.scale`, i.e. Major. Key (basics.h:233-245) carries only KeyType Major/Minor and Key::get (engine/src/music.cpp:122) throws on anything else, which is why the code folds to Major rather than erroring. templates_json.h:1253 reads scaleName as an arbitrary string with default 'Major'; no validation. Note that a section WITH keyName or scaleOverride does go through Scale::get(secTonic, secType) with secType = tmpl.scaleName (:326-327), so the exotic type is honored there — the silent collapse is only for sections inheriting the piece scale, exactly as the finding states. No tracked score uses a non-Major/Minor scaleName (40 Major, 1 Minor across scores/ and patches/), so latent, not live.

## F061 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/music/composer.h:313

**Piece/section/passage key levels are derived by string concatenation and splitting at three sites, with two concrete leaks (exotic piece scale dropped; passage override uses piece tonic)**

Reporters: arch-music-model

Evidence: `313-314: `piece.key = Key::get(tmpl.keyName + " " + (tmpl.scaleName == "Minor" ? "Minor" : "Major"));` 338-340: `Scale secScale = (!hasSectionKey && sd.scaleOverride.empty()) ? piece.key.scale : Scale::get(secTonic, secType);` 330-336 splits sd.keyName on a space; 370-371 rebuilds a Key string again. 890-891: `passage.scaleOverride = Scale::get(tmpl.keyName, passIt->second.scaleOverride);` while 320-323 says the section-level override was fixed precisely because it 'realized scaleOverride at the PIECE tonic'.`

Why: (a) A piece-level `scaleName` other than Major/Minor (Dorian, Harmonic Minor -- all are in ScaleDef's table) is silently reduced to Major for piece.key and, because sections inherit `piece.key.scale`, every section loses the authored scale unless it repeats it in scaleOverride. (b) A passage scaleOverride in a section whose keyName is G is resolved on the piece tonic C, repeating the bug just fixed one level up. The four-levels model exists in the data (Piece::key, Section::scale, KeyContext, chords) but is computed by ad-hoc text handling with no single resolver.

Recommendation: Introduce a typed KeySpec {tonic, scaleType} on PieceTemplate/SectionTemplate/PassageTemplate (JSON keeps the strings), one `resolve_key(parent, override)` function, and resolve passage overrides against the enclosing Section's tonic. Key::get should take (tonic, type) rather than a joined string.

- judge: Both leaks are real defects in the core four-level key resolver, but neither is hit by any current artifact: every piece-level scaleName in scores/ is Major (40) or Minor (1), and no score uses a passage-level scaleOverride, so the wrong-tonic resolution at :890 is latent today. When hit it is audible (passage pinned to the piece tonic inside a section in another key), which keeps it at medium rather than low. Recommendation is concrete; the minimal version is a single resolve_key(parentTonic, parentType, override) used at :313, :338 and :890 with :890 passing the enclosing section's tonic -- the typed KeySpec / Key::get(tonic,type) overload is nice-to-have on top.

## F062 [CONFIRMED-BY-MATT'S-CLAUDE] high (reporter: high) workaround-hack — engine/include/mforce/music/composer.h:404

**compose(Piece&, const PieceTemplate&) mutates the template through const_cast at three sites, plus a const_cast<Section*> on an object reachable non-const**

Reporters: arch-build-headers, arch-modern-cpp, arch-music-model, music-composer

Evidence: `:405 `::mforce::realize_motifs(const_cast<PieceTemplate&>(tmpl), rng_);` :820 `Locus locus{&piece, const_cast<PieceTemplate*>(&tmpl), 0, 0};` :859 `Locus locus{&piece, const_cast<PieceTemplate*>(&tmpl), sectionIdx, partIdx};` then :269 `locus.pieceTemplate->parts[locus.partIdx].passages[sectionName] = planned;` writes back; :879-881 `const_cast<Section*>(section)->harmonyTimeline.set_segment(...)` although `piece` is a non-const Piece& in the same function.`

Why: The public contract (IComposer::compose, ClassicalComposer, Composer::compose) promises not to touch the template, but realized motif pools, planned passage templates and section harmony are all written into it. Modifying an object that is actually const is UB; today callers happen to pass non-const locals (main.cpp:554, engine_tests:1876) so it works by accident, and the CLI only avoids cross-seed contamination because it copies baseTmpl per iteration. The comment at 400-403 already calls this transitional; it has outlived the refactor it was transitional for (Locus declares pieceTemplate non-const by design, locus.h:11-17).

Recommendation: Change Composer::compose / compose_one_passage / IComposer overloads to take `PieceTemplate&`, delete all three const_casts and the realize_motifs_ wrapper (call the free function directly). For the Section case, look the section up through `piece.sections` as non-const (`Section* section`) and drop the cast.

Also reported as: compose() promises `const PieceTemplate&` but mutates the template through five const_casts; the recipe doubles as scratch state | const_cast is used to mutate the PieceTemplate and Section that compose() declares const | compose(const PieceTemplate&) mutates the template through const_cast

- judge: Four reporters converged. The public compose(const PieceTemplate&) contract is false: realized motif pools, planned passages and chord-walker harmony are all written through const_cast, so the template is scratch state and only works because every caller passes a non-const local (main.cpp copies baseTmpl per seed to avoid contamination). Latent UB on any genuinely const template plus a misleading API on the composer's main entry point is a structural smell with concrete cost; the comment itself says the cast is transitional. Not audible today, so not critical. Fix is small and fully specified: PieceTemplate& through IComposer/Composer/compose_one_passage, delete the three casts and the realize_motifs_ wrapper, and take Section* non-const from piece.sections at :879.

## F063 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/music/composer.h:420

**Beat bookkeeping disagrees across the pipeline: section_start_beat_ ignores truncateTailBeats while the realize loop, Conductor and the CLI all use effective (truncated) beats; totalBars truncates partial bars**

Reporters: music-composer

Evidence: `:420-427 `float beat = 0.0f; for (const auto& s : piece.sections) { if (s.name == sectionName) return beat; beat += s.beats; }` vs :441 `float effectiveBeats = sec.beats - sec.truncateTailBeats;`, conductor.h:553 same, main.cpp:874 `totalBeats += std::max(0.0f, sec.beats - sec.truncateTailBeats);`. Also :671 `int totalBars = int(sec.beats / beatsPerBar);`.`

Why: With truncateTailBeats > 0 on section k, section k+1's events are placed at the full-length offset, leaving a hole of truncateTailBeats of silence and pushing the piece past the render length the CLI computes from effective beats (the tail gets cut). In pattern mode a section whose length is not a whole number of bars (e.g. 10 beats in 4/4) gets no chords for the remainder. Also `section_start_beat_` silently returns 0.0f for an unknown section name.

Recommendation: Define one notion of section start (sum of effective beats) in one place — a `Piece::section_start_beat(idx)` helper using effective beats — and use it from Composer, Conductor and the CLI. Use ceil for totalBars (or walk the pattern until pos >= sec.beats) so partial bars are covered.

- judge: Split cost. The truncateTailBeats half is theoretical today: only the DUN parser sets it (dun_parser.h:275/376), dun_to_piece builds the Piece directly without Composer, and templates_json has no truncate field, so section_start_beat_ never sees a truncated section. The totalBars = int(sec.beats / beatsPerBar) half (:671) is reachable and audible: a pattern-mode harmony part in a section that is not a whole number of bars gets no chords for the remainder. Ceil/walk-until-pos>=beats is concrete and resolves that. Caution on the helper recommendation: Conductor::perform (conductor.h:545-567) loops sections and calls perform_events on the whole elementSequence each time with an accumulating beatOffset, while Composer already places events at absolute beats (section_start_beat_ and realize_chord_parts_ both accumulate full sec.beats); if that reading is right the Conductor should not be offsetting at all, so 'use the helper from Conductor' would be the wrong fix there -- worth a skeptic check as a possible separate double-play bug on multi-section pieces.

## F064 [UNVERIFIED] medium (reporter: medium) elegance — engine/include/mforce/music/composer.h:478

**realize_phrase_to_events_ takes 11 parameters including two out-params and a defaulted trailing bool**

Reporters: music-composer

Evidence: `:478-487 `float realize_phrase_to_events_(Part& part, const Phrase& phrase, const Scale& baseScale, float startBeat, DynamicState& dynamics, const std::vector<DynamicMarking>& markings, int& nextMarking, float passageBeatOffset, const Section& section, float maxSectionBeats, bool keyAware = false)``

Why: The call site (:471-474) has to thread cursor state (currentBeat, dynamics, nextMarking) through by reference, the function mixes four concerns (pitch walking, key-context snapping, dynamics marking cursor, section-end truncation) and the defaulted `keyAware` can be silently omitted. This is the function the critical realization fix will touch, so its shape matters.

Recommendation: Introduce a `RealizeCursor { float beat; float noteNumber; Scale scale; DynamicState dynamics; int nextMarking; }` owned by realize_passage_to_events_ and pass `(Part&, const Phrase&, const Section&, const RealizeCtx&, RealizeCursor&)`. Remove the default argument.

- judge: Borderline low, kept at medium for two concrete hazards rather than taste: the defaulted keyAware=false can be silently omitted at a new call site and disable key-aware realization with no diagnostic, and three pieces of cursor state (beat, dynamics, nextMarking) are threaded by reference through an 11-parameter signature in the function the realization fixes (F063/F065) will modify. No runtime cost. RealizeCursor + removing the default is concrete and resolves it; should be done alongside the F065 change rather than as a standalone pass.

## F065 [UNVERIFIED] high (reporter: medium) duplication — engine/include/mforce/music/composer.h:524

**Two sources of truth for section harmony; the melody chord-tone path hand-scans Section::chordProgression and resolves at the static section scale, while the chord-part path uses HarmonyTimeline::chord_at, key contexts and passage-local progressions**

Reporters: music-composer

Evidence: `:524-537 `if (isChordFig && section.chordProgression) { const auto& prog = *section.chordProgression; ... for (int ci = 0; ci < prog.count(); ++ci) { if (chordBeat + prog.pulses.get(ci) > sectionBeat) ...` then :538 `prog.chords.get(chordIdx).resolve(section.scale, kBaseOctave)`; versus :747 `const ScaleChord* sc = timeline->chord_at(pos - beatOffset);`, :686 `Scale activeScale = sec.active_scale_at(beatInPassage);`, :615-619 passage-local `localTimeline`. Both stores are written in lockstep at :345-348/:379-382 and :879-881.`

Why: Section::chordProgression and Section::harmonyTimeline must be kept in sync by every writer (two writers today), and the two readers already diverge: a modulating section (keyContexts) or a passage-local progression voices the accompaniment in the new key / local chords while melody ChordFigures still step through the section progression in the section scale. The manual chord scan is O(chords) per note and reimplements chord_at.

Recommendation: Make HarmonyTimeline the single store (drop Section::chordProgression or make it a derived view), have realize_phrase_to_events_ call `section.harmonyTimeline.chord_at(sectionBeat)` and resolve against the same active scale the chord path uses, and honour the passage-local progression via Passage (the Locus already carries `harmony`).

- judge: Two stores (Section::chordProgression, Section::harmonyTimeline) written in lockstep at :345/:379/:879 and read by two paths that already disagree: melody ChordFigures resolve against the static section.scale while the chord part uses active_scale_at and the passage-local timeline. The consequence is audible -- melody chord tones in the old key against accompaniment in the new key -- as soon as keyContexts (the shipped Bruckner-pedal modulation) or a passage-local progression meet a ChordFigure melody. Not exercised today (no score uses keyContexts; the only passage-local progressions are on harmony parts in two baselines), which is the only thing keeping this from critical. Recommendation resolves it; the minimal, lower-risk version is to make realize_phrase_to_events_ read section.harmonyTimeline.chord_at(sectionBeat) and resolve against the same `scale` variable the keyAware branch already updates, then retire chordProgression as a derived view in a second step.

## F066 [UNVERIFIED] medium (reporter: medium) workaround-hack — engine/include/mforce/music/composer.h:587

**RealizationStrategyRegistry is dead weight: `realizationStrategy` is parsed but never read, 'block' is hard-coded, and the rhythm-pattern strategy's logic is re-implemented inline**

Reporters: arch-music-model

Evidence: `587: `RealizationStrategy* blockStrat = realReg.resolve("block");` 730: `RealizationRequest realReq{chord, pos, dur, bar + 1, nullptr};` 731: `blockStrat->realize(realReq, part->elementSequence);` -- `passTmpl->realizationStrategy` is never consulted (grep: only templates.h:523, templates_json.h:880/1023-1024). The pattern walk at 736-751 (`for (float dur : pattern) { if (dur < 0) { pos += (-dur); continue; } ... emit_chord(*sc, pos, dur);`) duplicates RhythmPatternRealizationStrategy::realize at realization_strategy.h:109-121. 732: `prevChord = &part->elementSequence.elements.back().chord();` assumes the strategy's last emitted Element is a Chord.`

Why: A registry with one reachable entry and a template field that round-trips but does nothing is misleading to authors and reviewers; the duplicated pattern walk is the kind of copy that drifts (the inline version samples the timeline per strike, the strategy version repeats one chord). The `.back().chord()` read would throw std::bad_variant_access the day the rhythm_pattern strategy (which emits a trailing Rest for negative tail entries, realization_strategy.h:113) is actually wired.

Recommendation: Resolve the strategy by `passTmpl->realizationStrategy` (default 'block'), pass the RhythmPattern through RealizationRequest, delete the inline loop, and have `realize()` return the voiced Chord it emitted so prevChord does not depend on the last Element's type. If the registry is not wanted yet, delete it and the template field instead of carrying both.

- judge: Concrete, user-visible cost at authoring level: realizationStrategy round-trips through JSON (templates_json.h:880/1023) and is never read, so an author setting 'rhythm_pattern' gets a silent no-op. The inline pattern walk at :736-751 duplicates RhythmPatternRealizationStrategy with different semantics (per-strike timeline sampling vs one chord), and .back().chord() at :732 is safe only because 'block' always emits exactly one Chord. No audio defect today, so medium. Recommendation is concrete either way (wire the field + pass RhythmPattern via RealizationRequest and have realize() return the voiced Chord, or delete the registry and field); the delete option is the smaller change if the registry is not wanted yet.

## F067 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/music/composer.h:736

**RhythmPatternRealizationStrategy is registered but never resolved; its per-bar pattern walk is reimplemented inline in realize_chord_parts_**

Reporters: music-composer

Evidence: `composer.h:224 `realReg.register_strategy(std::make_unique<RhythmPatternRealizationStrategy>());` but :587 `RealizationStrategy* blockStrat = realReg.resolve("block");` is the only resolve; :742-749 `for (float dur : pattern) { if (dur < 0) { pos += (-dur); continue; } ... emit_chord(*sc, pos, dur); pos += dur; }` duplicates realization_strategy.h:111-121 `for (float dur : pattern) { if (dur < 0.0f) { out.add({beat, Rest{-dur}}); beat += -dur; } else { ... out.add({beat, c}); beat += dur; } }`. templates.h:521 still says rhythmPattern 'is consumed by the "rhythm_pattern"' strategy.`

Why: The RealizationStrategy abstraction was introduced (spec 2026-04-22) so chord-event expansion could be swapped per passage, but the composer hard-wires 'block' and does the pattern expansion itself, so the registry and PassageTemplate::realizationStrategy are dead weight, the two walks can drift (one emits Rests, the other skips), and readers are misled by the templates.h comment.

Recommendation: Either resolve the strategy named by the passage template and let RhythmPatternRealizationStrategy do the walk (passing the voiced chord via a per-strike callback, since voicing must sample the timeline per strike), or delete RhythmPatternRealizationStrategy and the registry entry and fix the templates.h comment. Also fold the duplicated Block fallback at realization_strategy.h:104-106 into a call to BlockRealizationStrategy.

- verifier: Evidence confirmed verbatim: composer.h:224 registers RhythmPatternRealizationStrategy; :587 resolve("block") is the only RealizationStrategyRegistry::resolve in engine/ and tools/; :730 always passes nullptr as RealizationRequest.rhythmPattern, so even the registered strategy could only ever take its Block fallback; :736-751 walks rp->pattern_for_bar inline and SKIPS negative entries while realization_strategy.h:111-121 emits Rest elements for them (confirmed divergence). PassageTemplate::realizationStrategy (templates.h:523) is parsed/serialized by templates_json.h:880/1023 but never read by the composer. templates.h:521 comment ('consumed by the "rhythm_pattern" strategy') and realization_strategy.h:14 / templates.h:374 are all stale. Block fallback dup at realization_strategy.h:104-106 confirmed. No in-tree scores/library, baselines or patches JSON uses rhythmPattern or realizationStrategy at all, so this is dead abstraction + misleading comments, not a runtime defect: medium.
- verifier: Verified. composer.h:224 registers RhythmPatternRealizationStrategy; a repo-wide grep (engine/ + tools/) finds exactly one RealizationStrategyRegistry::resolve call, composer.h:587 resolve("block"). PassageTemplate::realizationStrategy is parsed (templates_json.h:1023-1024) and written (:880) but read nowhere in composer.h, so an authored "realizationStrategy":"rhythm_pattern" is silently ignored. composer.h:742-749 is the same per-bar pattern walk as realization_strategy.h:111-121, with the drift the finding names (inline walk skips negative entries; the strategy emits Rest elements and uses one chord per bar, while the inline walk re-samples timeline->chord_at per strike). templates.h:520-522 comment confirmed. realization_strategy.h:104-106 is a verbatim copy of BlockRealizationStrategy::realize (:86-88). Consequence follows. Severity medium: dead registry entry plus a JSON field that is accepted and ignored with no diagnostic.
- judge: Concrete cost: PassageTemplate::realizationStrategy is serialized in JSON (templates_json.h:880/1023) yet realize_chord_parts_ hard-resolves "block" (composer.h:587), so a score that sets "rhythm_pattern" is silently ignored, and the two walks already differ (strategy emits Rest elements, inline walk skips). No audible defect today because only the inline walk runs, so medium not high. The delete option is the cheaper concrete fix; the callback option is also concrete. Fix the templates.h:521 comment either way.

## F068 [CONFIRMED] medium (reporter: high) soundness — engine/include/mforce/music/composer.h:820

**No-template fallback composes with a Locus pinned to (section 0, part 0) regardless of the actual part/section**

Reporters: arch-music-model, music-composer

Evidence: `820-821: `Locus locus{&piece, const_cast<PieceTemplate*>(&tmpl), 0, 0}; return this->compose_passage(locus, passTmpl);` reached from compose_passage_ line 895 `passage = generate_default_passage_(piece, tmpl, scale);` for any (part, section) with no PassageTemplate. compose_passage then persists the synthesized plan into the wrong slot: 268-269 `const auto& sectionName = locus.piece->sections[locus.sectionIdx].name; locus.pieceTemplate->parts[locus.partIdx].passages[sectionName] = planned;` and DefaultPassageStrategy composes in section 0's scale: 1415 `const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;` with the cursor from part 0's realized passage via pitch_before(locus).`

Why: For a multi-section or multi-part template that omits a passage, the fallback is composed in the wrong section's key and continues from another part's running pitch, and it overwrites parts[0].passages[sections[0]] in the (mutated) template, clobbering an authored plan. The default `--compose` template built at main.cpp:560-563 has no passages at all, so every section after the first takes this path.

Recommendation: compose_passage_ already computes sectionIdx/partIdx (851-858) but only inside the template branch; hoist them and pass a correct Locus into generate_default_passage_. Add a test: two sections in different keys, no passage templates, assert each fallback passage's pitches lie in its own section's scale.

Also reported as: Fallback passage uses a hard-coded Locus{0,0}: it persists its synthetic template into part 0 / section 0 and composes in section 0's scale regardless of which (part, section) it is filling

- verifier: Evidence accurate: composer.h:820-821 builds Locus{&piece,tmpl,0,0}; reached from :895 for any (part,section) lacking a PassageTemplate; compose_passage :266-270 then writes the synthetic plan to tmpl.parts[0].passages[sections[0].name] unconditionally (indices 0,0 always pass the bounds check). That wrong-slot persistence is the consequence that actually follows, and it has teeth when parts[0] is a Harmony part: realize_chord_parts_ runs AFTER the melody loop and reads partTmpl.passages (:603-607, :615, :632), so an authored chordConfig/chordProgression/voicingSelector for section 0 would be replaced by the synthetic melodic plan. Two stated consequences do NOT follow: (1) 'composed in section 0's scale' - the synthetic template is Generate/Free figures with explicit phrase startingPitch and cadenceType 0, generate_figure (default_strategies.h) is scale-independent, pitch_before is never reached on that path, and realize_passage_to_events_ (:458) resolves pitches against the section the passage is stored under (:898 stores it under the correct sectionName), so realized pitches land in the right key; (2) 'continues from another part's running pitch' - pitch_before is not called. The main.cpp:560-563 example is wrong: the default --compose template has exactly one section and one part, so Locus{0,0} is correct there; only a loaded multi-section template with a melody part missing a section passage hits this, and a scan of 41 in-tree score/patch templates found zero such parts. Latent, fallback-only, genuine wrong-slot write: medium, not high.
- verifier: Core claim verified: composer.h:820-821 builds Locus{&piece, tmpl, 0, 0} for every fallback, and compose_passage (:264-269) persists the synthetic planned template into tmpl.parts[0].passages[sections[0].name] regardless of the real (part, section). Two of the three stated consequences do NOT follow: (a) 'composed in section 0's scale / continues from another part's running pitch via pitch_before' is false for this passage — both synthetic phrases carry startingPitch (:808, :813) and the figures are Generate/Free, so the sectionIdx-scale cursor branch at :1412-1427, the pitch_before calls at :1656/:1223/:967, and the shape strategies are never reached; the start pitch is in fact built from piece.key.scale handed in at :839/:895 (piece key, not section 0's), and realization re-walks steps in the real section's scale (:458, :549). (b) 'default --compose template at main.cpp:560-563 … every section after the first takes this path' is wrong: that fallback builds exactly one section ("Main") and one part, so Locus{0,0} is correct there; the bug needs an authored multi-section/multi-part template that omits a passage, and the mutated template is not serialized (--compose dumps the Piece, main.cpp:643). The one concrete harm I could confirm from code: if tmpl.parts[0] is a Harmony part with an authored section-0 passage, a Melody part missing any passage overwrites that slot before realize_chord_parts_ reads it at :603-607, discarding chordConfig/chordProgression/voicing for section 0. The baselines checked (template_mary_crawl, test_passage_chords_precedence) have full passage coverage, so nothing shipped hits it today. Fix is cheap (hoist sectionIdx/partIdx from :851-858); severity medium, not high.
- judge: Real correctness flaw: compose_passage persists the synthetic plan into parts[0].passages[sections[0]] (composer.h:266-270) and DefaultPassageStrategy composes in sections[0].scale, so a multi-section template with a missing passage gets the wrong key and a clobbered template slot. Kept at high rather than critical because it is latent: the only multi-section score in the tree (scores/baselines/template_binary.json) specifies every passage, and the default --compose template (main.cpp:560-563) is single-section so Locus{0,0} happens to be right there. Fix is a few lines: hoist sectionIdx/partIdx (851-858) above the branch, pass the real Locus plus section harmony into generate_default_passage_.

## F069 [CONFIRMED] medium (reporter: high) god-class — engine/include/mforce/music/composer.h:902

**composer.h hosts ~1180 lines (57% of the file) of eight concrete strategies' out-of-line bodies under a dependency rationale that is no longer true**

Reporters: arch-music-model, music-composer

Evidence: `:906-910 `These bodies live here — BELOW the Composer class — because they call ctx.composer->find_rhythm_motif() ... which require the full Composer definition.` Same claim at :1189-1192, :1313-1315, :1641-1644, :1842-1845. None of the hosted bodies references Composer: they use `locus.pieceTemplate->find_rhythm_motif` (:918), `StrategyRegistry::instance()` (:1274, :1584, :1724, :1874, :1967), `::mforce::rng::next()` (:963, :1126, :1165, :1199) and `piece_utils::pitch_before` (:967). Hosted: detail::resolve_rhythm/resolve_contour, ShapeCadentialApproach/Skipping/Stepping, DefaultFigure/Passage/Phrase, chord_walk, AlternatingFigure, PhraseAwareFigure.`

Why: Composer becomes the compile-time hub for the whole comp lane: composer.h must include every strategy header (:4-25) so the bodies can be defined, so any strategy edit recompiles every TU that touches Composer, and the file cannot be read as 'the composer'. Duplication hides in the hosted bodies because they sit far from their declarations: the Literal-note->FigureUnit conversion is copied verbatim at :1218-1239 and :1729-1745; the contour-transform chain at :939-944 and :1105-1110; the targetNet-adjust + pad/trim tail at :1111-1119, :1151-1158 and :1175-1181. (Logic of those strategies is exempt from this review; the hosting and the copies are composer.h's problem.)

Recommendation: Move each body back into its own header (they compile there today: nothing needs Composer complete). Delete the stale 'ctx.composer' comments. Then shrink composer.h's include list to strategy.h, strategy_registry.h, realization_strategy.h and the voicing/harmony headers it actually uses, and put the registration list in a separate `register_default_strategies()` (see the singleton finding). Fold the Literal conversion into one helper and the contour-transform chain into detail::apply_contour_transform.

Also reported as: composer.h is a 2081-line god-header; 1,170 lines are strategy bodies parked here on a stale justification

- verifier: Confirmed: hosted region is :902-2081 = 1180 lines (56.7% of 2081). Stale 'ctx.composer->' rationale at :906-910, :1189-1192, :1313-1315, :1641-1644, :1842-1845 (also repeated in default_strategies.h:57-61, :231-233, :428-430 and shape_strategies.h:25-27). awk over lines 902+ finds 'Composer' only in comments; no code references it, and Locus (locus.h) carries no composer pointer, so nothing hosted needs Composer complete. Hosted list matches. All three duplication sites confirmed (Literal :1218-1239 vs :1729-1745; contour chain :939-944 vs :1105-1110; targetNet+pad/trim :1111-1119, :1151-1158, :1175-1181). Caveats on the stated cost: the include list :4-25 is also needed by the constructor's registration (:166-224), so moving bodies alone does not shrink it (recommendation already concedes this); composer.h reaches only 3 tool TUs (engine_tests, mforce_cli, test_figures via classical_composer.h), so the recompile cost is real but bounded. Stale comments + real duplication = worth fixing; 'high/god-class' overstates it given the engine is header-heavy by design and the TU count is 3. Overlaps F070 and F072.
- verifier: Verified. The 'ctx.composer->' rationale at :906-910, :1189-1192, :1313-1315, :1641-1644, :1842-1845 is stale: grep of composer.h for composer->/ctx.composer hits only comments, and no hosted body names Composer — they use locus.pieceTemplate-> (:918, :936, :1243, :1249, :1360), StrategyRegistry::instance() (:1274, :1584, :1724, :1874, :1967), ::mforce::rng::next (:963, :1126, :1165, :1199), piece_utils::pitch_before (:967, :1223, :1656, :1868, :1974). default_strategies.h:21 already says 'no forward decl needed here' while :57-61 and :231-233 still assert the opposite. Include list :2-34 confirmed; measured closure of composer.h = 50 engine headers; it is included by exactly three tool TUs via classical_composer.h (mforce_cli, test_figures, engine_tests). Line span 902-2079 is ~1178 of 2081 lines (57%). Copies confirmed: Literal conversion :1218-1239 vs :1729-1745; contour chain :939-944 vs :1105-1110; targetNet+pad/trim :1111-1119, :1151-1158, :1175-1181. Caveat on the recommendation: 'they compile there today' is not free — the bodies also depend on composer.h-local helpers (detail::resolve_rhythm/resolve_contour :915-957, chord_walk :1820-1837) and on headers the strategy headers do not include (anchor_selector.h, pitch_walker.h, figure_transforms.h, piece_utils.h). Severity: medium, not high — the only concrete cost is three TUs recompiling plus misleading comments; no CLAUDE.md rule is implicated and the engine is header-heavy by design (engine/src holds DSP .cpp files only).
- judge: Cost is named and concrete, not merely header-heavy layout: strategy headers are not self-contained (constructing a strategy without composer.h included links with unresolved compose_* bodies), every strategy edit recompiles all 11 composer.h TUs, and the 'needs full Composer' comments at 906-910/1189-1192/1313-1315/1642-1644/1842-1845 are false. Verified: no hosted body references Composer in code, only in comments, so the bodies can move back to their declaring headers unchanged (still inline, no .cpp needed). The Literal/contour duplication sub-items overlap F072; judge those there.

## F070 [DISPUTED] medium (reporter: high) workaround-hack — engine/include/mforce/music/composer.h:902

**composer.h is the hidden definition site for member functions of classes declared in six other headers, papering over a Composer<->strategy include cycle**

Reporters: arch-build-headers

Evidence: `composer.h:902-911 "These bodies live here — BELOW the Composer class — because they call ... which require the full Composer definition. Placing them in shape_strategies.h (where Composer is only forward-declared) would cause incomplete-type errors."; out-of-line definitions at :961 `inline MelodicFigure ShapeCadentialApproachStrategy::compose_figure(`, :1124 ShapeSkippingStrategy, :1163 ShapeSteppingStrategy, :1195 DefaultFigureStrategy::compose_figure, :1318 DefaultPassageStrategy::compose_passage, :1647 DefaultPhraseStrategy::compose_phrase, :1847 AlternatingFigureStrategy::compose_passage, :1915 PhraseAwareFigureStrategy::compose_passage`

Why: The strategy headers (default_strategies.h, shape_strategies.h, alternating_figure_strategy.h, phrase_aware_figure_strategy.h) are not self-contained: because every virtual is `inline`, their vtables are emitted in whichever TU constructs them, and that TU must also have included composer.h or the link fails with unresolved compose_* entries. This is a logical include cycle hidden by header layout, it makes composer.h a 2,081-line file whose closure is 51 headers + json (the single compile point of the comp lane: 73-79 headers per tool TU, /bigobj on mforce_cli), and it defeats incremental builds — touching any strategy body recompiles every TU that includes composer.h.

Recommendation: This is the first header/source split to make. Create engine/src/composer.cpp holding Composer's non-trivial methods (setup_piece_, realize_event_sequences_, realize_chord_parts_, compose_passage_) and engine/src/strategies.cpp holding the eight out-of-line compose_* bodies; keep only the class declarations in the headers and forward-declare Composer there. composer.h drops to ~300 lines, the strategy headers become independently includable, and the comp lane gets separately compilable units (expected: comp-lane rebuild after a strategy edit becomes one TU instead of four 70+-header tool TUs).

- verifier: Verified: the 8 out-of-line bodies at the cited lines are declared `override` with no body in default_strategies.h:62/239/266, shape_strategies.h:37/44/51, alternating_figure_strategy.h:31, phrase_aware_figure_strategy.h:37 and defined `inline` only in composer.h, so a TU that constructs any of them without composer.h cannot link; period_passage_strategy.h:155 constructs DefaultFigureStrategy, making that header equally non-self-contained. Include closure of composer.h computed exactly = 51 engine headers. Tool TU closures = 73 (mforce_cli), 79 (engine_tests), 70 (test_figures): three TUs, not 'four'. /bigobj confirmed at tools/mforce_cli/CMakeLists.txt:5 but its attribution to composer.h is unproven (that main.cpp is itself very large). Premise caveat: there is no Composer<->strategy include cycle today - strategy.h does not even forward-declare Composer, and none of the bodies uses it - so the layout is a leftover, not papering over a live cycle; the bodies can return to their own headers with piece_utils.h / anchor_selector.h / pitch_walker.h includes (none of which pull composer.h or the strategy headers), without any header/source split. Per CLAUDE.md the header layout is only a problem with concrete cost; the concrete costs here are the link-trap and 3 TUs recompiling, so medium. Largely the same defect as F069.
- verifier: The out-of-line definitions exist at the cited lines (:961, :1124, :1163, :1195, :1318, :1647, :1847, :1915), but the framing and the consequence do not hold. (1) 'six other headers' is four: shape_strategies.h, default_strategies.h, alternating_figure_strategy.h, phrase_aware_figure_strategy.h. (2) There is no Composer<->strategy include cycle to paper over: no strategy header includes composer.h, and no hosted body references Composer at all (see F069) — the placement is a leftover from a retired ctx.composer design, not a cycle workaround. (3) 'that TU must also have included composer.h or the link fails' is overstated: the bodies are `inline` with external linkage (COMDAT), so any link containing one TU that includes composer.h resolves them, and in this repo every TU that constructs these strategies does so through Composer's ctor (composer.h:166-219) — all three tool TUs include classical_composer.h -> composer.h. The real limitation is narrower (a strategy header cannot be unit-tested standalone; ill-formed NDR under [dcl.inline]). (4) 'four 70+-header tool TUs' is three; the header counts themselves check out (measured 73/70/79 engine headers; composer.h closure 50 vs claimed 51+json). (5) /bigobj is at tools/mforce_cli/CMakeLists.txt:5 but cannot be attributed to composer.h (main.cpp is 1200+ lines with its own include set). The valid kernel (hidden definition site, stale comments) is already F069; the .cpp-split recommendation is a layout change with no measured cost, which the review rules classify as low.
- judge: Same defect and same lines as F069; its distinct contribution is the recommendation to split into engine/src/composer.cpp + strategies.cpp. Since the hosted bodies do not depend on Composer, F069's header-local move already breaks the hidden include cycle and makes the strategy headers independently includable; the .cpp split is a larger change whose build-time benefit is predicted, not measured, and per the discuss-before-refactoring rule needs Matt's go-ahead as its own decision. Fold into F069 and downgrade; engine/src exists (13 .cpp files) so the split is not against convention if later wanted.

## F071 [CONFIRMED] low (reporter: low) smell — engine/include/mforce/music/composer.h:1012

**Self-declared 'for now' / throwaway code and design-note comments left in production headers**

Reporters: arch-hacks-census

Evidence: `composer.h:1016-1022 `// Actually: just use the interval. ... // For now, keep it simple and direct:`; chord_progression_builder.h:17 `// Throwaway-grade: hard-coded named lambdas, no context awareness`; structure.h:117-120 `// These types are kept in structure.h for now (rather than moved to a
// composer_internal.h) because strategy headers and the DUN parser still
// reference them by inclusion.`; figures.h:110 `StepSequence random_sequence(int length, float /*unused*/ = 0.3f)``

Why: These are honest, but none is tracked in a backlog item, so they will not be revisited by the autonomy workflow that owns cleanup; the unused parameter is API surface callers still fill in. Low cost individually; collectively they are the 'undocumented hacks' bucket this census was asked to find.

Recommendation: Either file each as a backlog line (so the ledger owns it) or resolve: drop the unused parameter, move Phrase/Passage to composer_internal.h, trim the brainstorm comment in compose_cadential to the rule actually implemented.

- verifier: All four quotes are at the cited lines: composer.h:1016-1022 ('Actually: just use the interval... For now, keep it simple'), chord_progression_builder.h:17 ('Throwaway-grade'), structure.h:117-120 ('kept in structure.h for now... composer_internal.h'), figures.h:110 (`float /*unused*/ = 0.3f`). Keyword grep of docs/autonomy (BACKLOG.md, GOALS.md, IDEAS.md, comp/dsp reports) finds no tracking entry for composer_internal, random_sequence, the ChordProgressionBuilder throwaway note or the compose_cadential brainstorm. Callers do still fill the dead parameter: default_strategies.h:118 passes skipProb and mforce_cli/main.cpp:247/253/259 pass 0.0f. One concrete item hides in that bucket: skipProb is derived from FigureTemplate::preferSkips (default_strategies.h:117, templates.h:119) and that call is preferSkips' only consumer, so the template field is a silent no-op. As filed (comments + backlog hygiene) this is low; the preferSkips no-op is the one line worth a backlog entry.
- verifier: All four evidence sites verified at the cited lines: composer.h:1016-1022 ('Actually: just use the interval… For now, keep it simple'), chord_progression_builder.h:17-24 ('Throwaway-grade…'), structure.h:117-120 ('kept in structure.h for now (rather than moved to a composer_internal.h)'), figures.h:110 (`random_sequence(int length, float /*unused*/ = 0.3f)`). The unused parameter is indeed still filled by callers: tools/mforce_cli/main.cpp:247/253/259 pass 0.0f and default_strategies.h:118 passes skipProb — a value the function ignores, so that caller's intent is silently dropped. No backlog tracking: grep of docs/autonomy for random_sequence|composer_internal|Throwaway|compose_cadential returns nothing; composer_internal appears only in 2026-04 plan/spec docs. Nit in the recommendation: the function is ShapeCadentialApproachStrategy::compose_figure, not 'compose_cadential'. Severity low as filed.
- judge: Nits with tiny concrete cost: figures.h:110 unused parameter is API surface callers still fill in; structure.h:117-120 and chord_progression_builder.h:17 are honest 'for now' markers with no backlog owner; composer.h:1016-1022 is a brainstorm comment that should be trimmed to the implemented rule (targetDegree = cadence==1 ? 4 : 0). Recommendation (file each as a backlog line or resolve) is concrete; low severity, no behavioral consequence.

## F072 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/music/composer.h:1215

**Literal-figure realization and the contour-motif transform chain are duplicated inside composer.h**

Reporters: arch-build-headers, arch-duplication

Evidence: `composer.h:1215-1239 (DefaultFigureStrategy::compose_figure, Literal case) `auto absoluteDeg = [&](const Pitch& p) { int d = DefaultPhraseStrategy::degree_in_scale(p, scale); return p.octave * scale.length() + d; }; ... for (auto& ln : figTmpl.literalNotes) { FigureUnit u; u.duration = ln.duration; if (ln.rest) { ...` vs :1726-1745 (DefaultPhraseStrategy::compose_phrase) `const Pitch cursorPitch = runningReader.get_pitch(); auto absoluteDeg = [&](const Pitch& p) { int d = DefaultPhraseStrategy::degree_in_scale(p, scale); return p.octave * scale.length() + d; }; ... for (auto& ln : figTmpl.literalNotes) {` — same body. Contour transform chain :939-944 (detail::resolve_contour) `if (ft.contourTransform == "invert") result = result.inverted(); else if (ft.contourTransform == "retrograde") ... "expand" ... "contract"` repeated verbatim at :1105-1110 (ShapeCadentialApproachStrategy). The pad/trim + targetNet tail is repeated at :1111-1119, :1151-1158, :1175-1181.`

Why: The phrase-level copy exists because of a cursor-visibility workaround documented at 1707-1722 ("Simplest fix: inline the Literal path's cursor-relative computation directly here"); two copies of the degree arithmetic will drift the next time step[0]/leadStep conventions move (they already moved once per arch_fc_and_step_zero).

Recommendation: Factor `MelodicFigure literal_to_figure(const FigureTemplate&, const Pitch& cursor, const Scale&)` and `StepSequence apply_contour_transform(StepSequence, const FigureTemplate&)` into a small header (or the planned strategies.cpp) and call them from all sites; the three shape strategies then share one `finalize_contour(contour, rhythm, ft.targetNet)`.

Also reported as: composer.h repeats its own Literal path, contour dispatch, pad/trim and cursor walks

- verifier: Every cited site confirmed. Literal path :1218-1239 (DefaultFigureStrategy::compose_figure) and :1729-1745 (DefaultPhraseStrategy::compose_phrase) are the same absoluteDeg lambda + loop, differing only in cursor source (pitch_before(locus) vs runningReader.get_pitch()). Contour-transform chain :939-944 (detail::resolve_contour) and :1105-1110 (ShapeCadentialApproach) are verbatim. targetNet adjust + pad/trim repeated at :1111-1119, :1151-1158, :1175-1181. The cursor-visibility workaround comment at :1707-1722 is present and says exactly what the finding quotes ('Simplest fix: inline the Literal path's cursor-relative computation directly here'). Drift risk is real: the two Literal copies encode the step[0]/leadStep convention independently. Same duplications are also listed inside F069; orchestrator should fold.
- verifier: Verified. Literal-note -> FigureUnit conversion at composer.h:1218-1239 (DefaultFigureStrategy::compose_figure) and :1728-1745 (DefaultPhraseStrategy::compose_phrase) is the same body; the only difference is the cursor source (piece_utils::pitch_before(locus) vs runningReader.get_pitch()), and the workaround rationale is the comment at :1707-1722. Contour-transform chain :939-944 (detail::resolve_contour) and :1105-1110 (ShapeCadentialApproachStrategy) is verbatim. targetNet adjust + pad/trim appears at :1111-1119, :1151-1158, :1175-1181 — note one existing drift a shared helper must preserve: in the cadential strategy the targetNet adjust sits inside the contour-motif override branch (:1101-1116) and so applies only when a motif was supplied, whereas skipping/stepping apply it unconditionally. Consequence (two copies of the degree arithmetic can drift) follows. Medium as filed.
- judge: Cited lines are in composer.h (not an exempt file), but the code logically belongs to the exempt DefaultFigureStrategy/DefaultPhraseStrategy/Shape* classes; if the exemption is meant by class ownership rather than file, drop this. Concrete cost: the two Literal-to-FigureUnit copies (1218-1239 vs 1729-1745) are identical except for the cursor source (pitch_before(locus) vs runningReader), so a literal_to_figure(figTmpl, cursor, scale) helper resolves both cleanly; the contour chain and pad/trim tail are repeated three times. Medium because step-convention drift already happened once (arch_fc_and_step_zero) and no present defect exists.

## F073 [CONFIRMED] low (reporter: medium) god-class — engine/include/mforce/music/composer.h:1318

**Oversized functions and classes in headers: a 320-line compose_passage with nested lambdas and local structs, a 185-line realize_chord_parts_, a 620-line PitchedInstrument, an 800-line Partials**

Reporters: arch-build-headers

Evidence: `composer.h:1318 `inline Passage DefaultPassageStrategy::compose_passage(` ... :1637 `}` (local `struct PassageAttempt` :1374, `struct PhraseCand` :1497, lambdas `run_attempt` :1384, `is_derived`, `rerollable`); composer.h:585-770 `void realize_chord_parts_(` with the `emit_chord` lambda :678-734; instrument.h:99-717 `struct PitchedInstrument final : Instrument {` (voice pool :187-215, capture :225-240, held line :382-392, glide :420-429, play_note :521-656, finish_line :662-711); partials.h:266-1066 `struct Partials : ValueSource, IPartials {` with ~70 data members (:983-1065)`

Why: These bodies are inline in headers, so every consumer TU re-parses and re-instantiates them; the walk3 best-of-N search (:1339-1636) is not unit-testable in isolation because it lives inside a lambda inside a strategy method, and the held-line state machine shares one class with voice-pool rotation and offline capture.

Recommendation: Split along the seams the code already names: a `PassageSearch` helper (candidate generation + pick_top_k) separate from phrase composition; `HeldLine` and capture into their own small classes owned by PitchedInstrument; move the non-hot method bodies to engine/src (see the composer.h split finding).

- verifier: Evidence verified at every cited location: compose_passage spans composer.h:1318-1637 (320 lines) with local `struct PassageAttempt` :1374, `struct PhraseCand` :1497, lambdas `is_derived` :1367, `rerollable` :1370, `run_attempt` :1384; realize_chord_parts_ :585-770 with `emit_chord` lambda :678-734; `struct PitchedInstrument final : Instrument` at engine/include/mforce/render/instrument.h:99-717 with voice pool :187-215, capture :225-240, HeldLine :382-392, make_glide :420-429, play_note :521, finish_line :662; `struct Partials : ValueSource, IPartials` partials.h:266-1066 with the member block :983-1065 (dozens of members, '~70' is in the right ballpark). The structural claim is true. The stated COSTS are thin, though: composer.h is included only via classical_composer.h, which reaches exactly 3 TUs (tools/engine_tests/main.cpp, tools/mforce_cli/main.cpp, tools/test_figures/main.cpp), and instrument.h reaches ~5 TUs, so 're-parses in every consumer TU' is a small, bounded compile cost, not a concrete pain. The 'not unit-testable in isolation' point is partly undercut: the search primitives it composes (select_anchors, pick_top_k) ARE directly tested in tools/engine_tests/main.cpp (:1416-1605, :1812-1821); only the candidate/attempt loop itself needs a full Locus/Piece. No correctness or RT-safety issue; CLAUDE.md treats header-heavy layout as a problem only with concrete cost, and the cost shown here is marginal. Maintainability refactor candidate, not a defect — low.

## F074 [CONFIRMED] medium (reporter: medium) smell — engine/include/mforce/music/composer.h:1343

**Engine library code reads environment variables and writes diagnostics straight to std::cerr/stderr; <iostream> is a transitive dependency of 12 headers**

Reporters: arch-build-headers

Evidence: `composer.h:1343 `harmonicMode && std::getenv("MFORCE_ANCHOR_LOG") != nullptr;` (and default_strategies.h:91 `std::getenv("MFORCE_FIGURE_RANDOM")`); std::cerr sites composer.h:250, :260, :637, :654, :1586, :1631-1634, :2041-2049; instrument.h:306, :553, :651, :700 `std::fprintf(stderr, ...)`; composer.h:38 `#include <iostream>`; 11 engine headers include <iostream> directly`

Why: A JUCE plugin or a UI host has no stderr and cannot opt into env-var switches per instance; the engine's warnings (unknown strategy, containment failures) are therefore invisible exactly where they matter, and the one-off `[containment]` reports cannot be routed to the UI's status line. <iostream> in headers also adds static-init cost and compile weight to every includer.

Recommendation: Add a tiny `mforce::log(Level, std::string_view)` sink with a settable callback (default: stderr) in core/, replace the cerr/fprintf sites with it, and replace getenv gates with explicit fields on PassageTemplate/Composer options; drop <iostream> from headers.

- verifier: Evidence verified: `std::getenv("MFORCE_ANCHOR_LOG")` at composer.h:1343 and `std::getenv("MFORCE_FIGURE_RANDOM")` at default_strategies.h:91 (the only two getenv sites in engine/); std::cerr sites at composer.h:250, :260, :637, :654, :1586, :1631, :1633-1634, :2041-2049; `std::fprintf(stderr,...)` at render/instrument.h:306, :553, :651, :700 (the two `[containment]` reports are :652 and :701); `#include <iostream>` at composer.h:38; exactly 11 engine headers include <iostream> directly (finding says 'transitive dependency of 12' / '11 direct' — the 11 is exact). Consequence confirmed by the UI's own code: tools/mforce_ui/CMakeLists.txt:41 sets WIN32_EXECUTABLE TRUE, and main.cpp:4001-4003 and :12781 state that fprintf(stderr) is invisible in that build; the parent console is only attached for headless '-' modes (main.cpp:12958-12967). instrument.h is included by mforce_ui/main.cpp, so the [onset]/[containment] diagnostics genuinely vanish in a normal GUI run, and the UI already has a precedent for routing messages to its own status line instead of stderr (main.cpp:4277). Partial overstatement: composer.h is NOT reached by mforce_ui (only engine_tests, mforce_cli, test_figures include it via classical_composer.h), so the 'unknown strategy' warnings land on a working stderr today; the env-var gates are deliberate dev-diagnostic switches per the walk3 spec (docs/superpowers/specs/2026-09-22-comp-walk3-design.md). <iostream> static-init cost is negligible on MSVC. No RT-safety or correctness flaw; a settable log sink is a worthwhile cleanup given the GUI is the primary target — medium.

## F075 [CONFIRMED] low (reporter: low) soundness — engine/include/mforce/music/conductor.h:426

**ChordPerformer::perform_with_figure loops on elapsed < chordDurSeconds with no progress guard and indexes fig.elements without a count check**

Reporters: music-composer

Evidence: `:426-427 `while (elapsed < chordDurSeconds) { const auto& elem = fig.elements[elemIdx];` ... :463 `elapsed += elemDurSeconds;``

Why: A ChordRealization with zero elements, or whose elements all have duration 0, never advances elapsed: out-of-bounds read or an infinite loop. Today only the built-in Josie figures (all durations > 0) reach it, so the hazard is latent, but select_figure keys on an authored `chord.figureName` so a future JSON-defined figure would hit it.

Recommendation: Guard `if (fig.count() == 0) { perform_block(...); return; }` and break when a full cycle adds no time.

- verifier: Evidence exact: conductor.h:426-427 `while (elapsed < chordDurSeconds) { const auto& elem = fig.elements[elemIdx];`, :463 `elapsed += elemDurSeconds;`, :466 wraps elemIdx only AFTER the read. ChordRealization (figures.h:669-708) has no invariant on element count or duration; music_json.h:405-411 from_json accepts an empty `elements` array and unvalidated durations. A zero-element figure reads elements[0] out of bounds; all-zero durations never advance `elapsed` (infinite loop whenever chordDurSeconds > 0). Consequence is latent as the finding says: namedFigures is populated only by register_josie_figures (all durations > 0), and the `figures` map and `defaultFigure` are never populated anywhere (Grep across engine/ and tools/), so select_figure today returns a Josie figure or nullptr (→ perform_block). No JSON path currently loads a ChordRealization into the performer; only a future authored figure would hit it. Not a hot render path (score-time scheduling).
- verifier: Evidence is exactly at conductor.h:426-427 (`while (elapsed < chordDurSeconds) { const auto& elem = fig.elements[elemIdx];`) and :463 (`elapsed += elemDurSeconds;`); elemIdx wraps at :466 via `fig.count()` but there is no empty check before the first index and no progress guard. Consequence follows from the code: ChordRealization::elements is a plain std::vector (figures.h:680), so zero elements reads elements[0] out of bounds, and all-zero durations leave elapsed unchanged forever while play_note is called every iteration. Reachability is as the finding says, latent: namedFigures is populated only by register_josie_figures (conductor.h:342-380, every duration > 0); the duration-keyed `figures` map and `defaultFigure` are never assigned anywhere in engine/ or tools/, so select_figure returns nullptr and perform_block runs for any non-Josie chord. music_json.h:405 has a from_json(ChordRealization) but grep finds no caller that feeds it into ChordPerformer. Real hole, no live input reaches it today; low.
- judge: Latent hazard only: the sole producers of ChordRealization today are the built-in figures with positive durations, so no current render can hit the OOB read or infinite loop; cost is theoretical until JSON-authored figures exist. The guard (count()==0 -> perform_block; break when a full cycle adds zero time) is a two-line defensive fix and resolves it.

## F076 [CONFIRMED] critical (reporter: critical) soundness — engine/include/mforce/music/conductor.h:545

**Conductor::perform(Piece) performs every part's entire elementSequence once per section, each pass shifted by the cumulative section length**

Reporters: arch-music-model, music-composer

Evidence: `for (const auto& section : piece.sections) { ... for (const auto& part : piece.parts) { ... if (!part.elementSequence.empty()) { perform_events(part, bpm, beatOffset, inst); } } beatOffset += effectiveBeats; }  — and perform_events (588-606) iterates ALL of part.elementSequence with `float absBeats = beatOffset + event.startBeats;``

Why: Composer stores events at ABSOLUTE beats (composer.h:440 passageStartBeat = section_start_beat_, :599/:730 `beatOffset + ...`), so for an N-section piece every note is played N times: once correctly and N-1 times shifted. Verified empirically on scores/baselines/template_binary.json (2 x 16 beats @110): the saved piece has 40 events ending at beat 31.875 (17.39 s), yet the instrument's containment log shows notes scheduled at t=22.64..26.11 s; 26.11 s = (31.875+16)*60/110 exactly. Section A is also re-struck on top of section B (RMS steps from ~0.09 to ~0.13 at 8-17 s). The outer section loop is a leftover from the tree-walk era (perform_passage per section, retired at Stage 8, see comment at 612-613); per-section `bpm = section.tempo` is also wrong against absolute beats if tempos ever differ.

Recommendation: Make perform(Piece) a single pass: for each part, perform_events(part, piece.sections[0].tempo, 0.0f, inst). If per-section tempo is wanted, convert beats->seconds via a tempo map rather than a per-section pass. Add an engine_tests case that composes a 2-section template and asserts play_note count == event count (or max scheduled start < total beats).

Also reported as: Conductor::perform(Piece) replays every Part's entire ElementSequence once per Section

- verifier: Evidence exact: conductor.h:548-567 outer loop over piece.sections, inner loop over piece.parts calling perform_events(part, bpm, beatOffset, inst) with beatOffset accumulating effectiveBeats; perform_events (:592-606) iterates ALL of part.elementSequence with absBeats = beatOffset + event.startBeats. Composer writes ABSOLUTE beats: composer.h:440 passageStartBeat = section_start_beat_() (sums prior sections' beats), :468 currentBeat seeded from it, :576 elementSequence.add({currentBeat, n}); chord parts :599-600 beatOffset += sec.beats, :738 barStart = beatOffset + bar*beatsPerBar, :730-731 emit at pos. Reproduced into the scratchpad: `mforce_cli --compose patches/library/winds/oboe1.json ... --template scores/baselines/template_binary.json` (2 x 16 beats @110) saves 40 events, first at beat 0.0, last at beat 31.875 (absolute, past section A's 16); the instrument's [containment] log shows a complete second pass of the same pitch sequence starting at t=8.73 s (=16*60/110) and ending at t=26.11 s (=(31.875+16)*60/110), beyond the 19.45 s render buffer — i.e. every note is scheduled twice, section A re-struck on top of section B. The per-section bpm remark also follows (perform_events converts absolute beats with section.tempo), though every current producer gives all sections one tempo. Blast radius today: among tracked scores only scores/baselines/template_binary.json has >1 section (one file, test_k467_walker.json, is not parseable JSON), so single-section comp templates are unaffected; but perform(Piece) is the entry point for --compose, --play, --josie, render_piece_to_wav and --dun, and multi-section is where the comp roadmap heads. Audible wrong output from the main perform entry point with no diagnostic: critical.
- verifier: Lines exact: perform(Piece) at conductor.h:545-568 loops sections, calls perform_events(part, bpm, beatOffset, inst) at :562 for every part on every section, then `beatOffset += effectiveBeats` at :566; perform_events (:588-606) iterates the whole elementSequence with `absBeats = beatOffset + event.startBeats` (:593). Composer emits ABSOLUTE beats: composer.h:440 `passageStartBeat = section_start_beat_(piece, sec.name)`, :468 currentBeat starts there, :576 `part.elementSequence.add({currentBeat, n})`; chord parts likewise accumulate beatOffset at :599/:738/:761/:767. So an N-section piece dispatches each event N times. Reproduced: `mforce_cli --compose patches/library/winds/oboe1.json <scratch> 1 --template scores/baselines/template_binary.json` -> saved piece has 2 sections (A 16, B 16 @110), 40 events, last at beat 31.875; stderr shows exactly 80 `[containment] note` reports (each note scheduled twice), last at t=26.11 s = (31.875+16)*60/110 for a 17.45 s piece. The +16-beat copy of section A lands on top of section B inside the render buffer, so every multi-section composed render is audibly wrong. git show 4f25d8b confirms the section loop is the surviving shell of the per-section tree-walk dispatch (the removed fallback was `perform_passage(it->second, ..., beatOffset, ...)`). The per-section-tempo remark is also correct in principle (absolute beats scaled by a later section's bpm) but untested today since all sections share tempo. run_play (main.cpp:705) and test_figures (:1168) go through the same perform(Piece).
- judge: Audible corruption of every multi-section comp-lane render on the primary path: section A is re-struck over section B and notes are scheduled past the piece end (verified on a tracked baseline score). Single-pass perform_events per part with beatOffset 0 fixes it directly; the per-section tempo concern is correctly routed to a tempo map rather than a per-section pass, and the proposed engine_tests assertion (play_note count == event count) would lock it.

## F077 [CONFIRMED] high (reporter: critical) soundness — engine/include/mforce/music/conductor.h:560

**Tree-built Pieces are silently inaudible: the only passage->event realization is a private Composer method, and Conductor drops parts with an empty elementSequence without a diagnostic**

Reporters: music-composer

Evidence: `conductor.h:560-563 `// Composer fully populates Part.elementSequence; that's all we read. if (!part.elementSequence.empty()) { perform_events(...) }`; composer.h:429 `void realize_event_sequences_(Piece& piece, ...)` is private and only called from compose() (:237); dun_parser.h:360-379 builds `part.passages[...]`/Passage trees and never touches elementSequence (grep: no writer in dun_parser.h).`

Why: tools/mforce_cli --dun (main.cpp:841 dun_to_piece -> :869 conductor.perform(piece)) therefore renders silence. Verified: `mforce_cli --dun scores/baselines/k467_bars_1_to_12.dun patches/library/winds/oboe1.json ...` prints `Rendered ... (48 beats @ 100 bpm, peak=0)`. A tracked baseline score has rendered silence since commit 4f25d8b (delete tree-walk path), which post-dates the multi-section DURN work (39a9b61/ba82d0f, 2026-04-13). The same trap applies to any hand-built or JSON-loaded Piece without events (--play path).

Recommendation: Lift realize_event_sequences_/realize_passage_to_events_/realize_phrase_to_events_ out of Composer into a free function `realize_events(Piece&)` (they only read Piece/Section/Passage), call it from Composer::compose and from dun_to_piece/run_dun/run_play. In Conductor, warn (or throw) when a part with passages but no events is encountered instead of skipping silently. Add a --dun smoke test asserting peak > 0.

- verifier: Evidence exact: conductor.h:560-563 skips any part with empty elementSequence, silently. realize_event_sequences_ (composer.h:429) sits under `private:` (:307) and its only caller is compose() at :237 (Grep: no other callers of realize_event_sequences_/realize_passage_to_events_/realize_phrase_to_events_ in engine/ or tools/). dun_to_piece (dun_parser.h:326-451) writes only part.passages[sectionName] (:445) and piece.parts (:448); Grep finds no elementSequence writer in dun_parser.h. run_dun: main.cpp:841 dun_to_piece → :869 conductor.perform(piece). Reproduced into the scratchpad: `mforce_cli --dun scores/baselines/k467_bars_1_to_12.dun patches/library/winds/oboe1.json out.wav` prints `Rendered ... (48 beats @ 100 bpm, peak=0)`. History claim checks out: 39a9b61 and ba82d0f are dated 2026-04-13, 4f25d8b 2026-04-22, and the 4f25d8b diff deletes exactly the `part.passages.find(section.name) → perform_passage(...)` fallback that --dun relied on; no test exercises dun_to_piece (Grep: only dun_parser.h:326 and main.cpp:841). --play (main.cpp:690 get<Piece>, :705 perform) loads events only from the `events` key (music_json.h:578), so a passage-only piece JSON is likewise silent. Severity: concrete, deterministic, silent failure of a documented CLI mode and a tracked baseline score for ~5 months; I rate it high rather than critical because the path is dormant (not on the active --compose comp lane), the CLI does print peak=0, and nothing is corrupted or lost — the fix is lifting the realize step out of Composer.
- verifier: Evidence exact: conductor.h:560-563 skips any part with an empty elementSequence, no diagnostic. composer.h:307 `private:` precedes realize_event_sequences_ at :429, whose only call is compose() at :237. dun_parser.h builds Passage trees and writes `part.passages[sectionName]` at :445; grep confirms no elementSequence writer in dun_parser.h. main.cpp run_dun (:815) does dun_to_piece (:841) then conductor.perform(piece) (:869) with nothing realizing events in between. Reproduced with the Sep 22 build: `mforce_cli --dun scores/baselines/k467_bars_1_to_12.dun patches/library/winds/oboe1.json <scratch>.wav` prints `Rendered ... (48 beats @ 100 bpm, peak=0)`. Commit chronology checks out: 4f25d8b (2026-04-22) deleted the `perform_passage` fallback that previously handled passage-only parts; 39a9b61/ba82d0f are 2026-04-13. The --play remark is trivially true via the same gate (Piece JSON loader at music_json.h:578 populates events only when present). Severity: concrete, silent correctness failure on a tracked baseline path, but confined to the dormant --dun/eventless-Piece entry points, not the main compose render; high rather than critical.
- judge: User-visible regression: a tracked baseline (.dun) renders peak=0 through the --dun CLI path with no diagnostic, and the same silent skip traps any hand-built or JSON-loaded Piece on --play. Rated high rather than critical because it kills a secondary input path (the template/compose path still produces audio) rather than corrupting primary output; bump to critical if --dun is treated as a supported release surface. Recommendation is concrete: lift realize_* out of Composer as a free function called from compose and dun_to_piece/run_play, make Conductor warn on parts-with-passages-but-no-events instead of skipping, add a peak>0 smoke test.

## F078 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/music/drift_voicing_profile_selector.h:40

**inversionProfiles/spreadProfiles config parsing is copy-pasted between Drift and Random selectors; VoicingProfile JSON parsing is duplicated between Scripted and templates_json.h**

Reporters: music-builders-voicing

Evidence: `drift :40-58 and random_voicing_profile_selector.h:31-49 are character-identical (`inversionProfiles_.clear(); if (cfg.contains("inversionProfiles") && cfg["inversionProfiles"].is_array()) { for (const auto& pj : ...) { if (!pj.is_array()) continue; std::vector<int> prof; ...`); scripted_voicing_profile_selector.h:30-42 vs templates_json.h:1047-1058 both hand-parse priority/allowedInversions/allowedSpreads (and already diverge: the latter also reads repeatPenalty/cadential).`

Why: Two copies have already drifted in behavior (scripted lacks the comp #8 fields); any future profile field has to be added in three places.

Recommendation: Add `voicing_profile_from_json(const nlohmann::json&)` next to VoicingProfile and a `parse_int_list_lists(cfg, key)` helper in voicing_profile_selector.h; both selectors and templates_json.h call them.


## F079 [UNVERIFIED] low (reporter: low) modern-cpp — engine/include/mforce/music/drift_voicing_profile_selector.h:72

**Hand-rolled Box-Muller with a literal pi instead of a Randomizer gaussian helper / std::numbers**

Reporters: music-builders-voicing

Evidence: `:73-76 `float u1 = rng_.value(); if (u1 < 1e-6f) u1 = 1e-6f; float u2 = rng_.value(); float z = std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * 3.14159265358979f * u2);` — randomizer.h has no gaussian draw.`

Why: The only gaussian step in the music layer lives inside one selector; the next user re-derives it. C++20 provides std::numbers::pi_v<float>, and Randomizer already owns the mt19937 that std::normal_distribution could use.

Recommendation: Add `float Randomizer::gaussian(float stddev)` (normal_distribution over `rng`) and call it here.


## F080 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/music/drift_voicing_profile_selector.h:88

**Non-static profile selectors drop the passage's authored repeatPenalty and cadential; composer injects the baseline only via dynamic_cast to StaticVoicingProfileSelector**

Reporters: music-builders-voicing

Evidence: `drift :88-94 `VoicingProfile p; p.priority = currentPriority_; ... return p;` (repeatPenalty/cadential left at 0/false); random_voicing_profile_selector.h:57-67 same; scripted_voicing_profile_selector.h:30-42 parses only priority/allowedInversions/allowedSpreads while templates_json.h:1050-1052 parses `repeatPenalty` and `cadential` for the baseline; composer.h:659-662 `if (auto* sw = dynamic_cast<StaticVoicingProfileSelector*>(profileSelector.get())) sw->configure(passIt->second.voicingProfile);` and :717-720 uses the selector's profile instead of the baseline.`

Why: Selecting "random", "drift" or "scripted" silently disables both comp #8 features the baseline profile asked for. The interface in voicing_profile_selector.h has no baseline hook, which is why the composer reaches for RTTI on one concrete subclass.

Recommendation: Add `virtual void set_baseline(const VoicingProfile&)` to VoicingProfileSelector (default stores it); have every selector start from the baseline and only override the fields it varies; remove the dynamic_cast in composer.h. Parse VoicingProfile from JSON in exactly one place (see duplication finding).


## F081 [REFUTED] low (reporter: low) build — engine/include/mforce/music/dun_parser.h:11

**Missing direct includes and an unused <iostream> in headers that fan out through templates.h to 19 TUs**

Reporters: music-json

Evidence: `#include <iostream>            // dun_parser.h:11 — no std::cout/cerr/cin in the file
std::unordered_map<std::string, Ornament> legacy = ...   // music_json.h:117, 153, 174 — no <unordered_map>
int(std::lround(p.note_number()))                        // music_json.h:244 — no <cmath>
snprintf(durBuf, sizeof(durBuf), "%g", dur);              // parse_util.h:158 — no <cstdio>
std::string_view(str).find('|')                           // parse_util.h:272 — no <string_view>
using json = nlohmann::json;   // namespace-scope alias in BOTH templates_json.h:9 and music_json.h:13`

Why: All compile today only via transitive includes from nlohmann/json.hpp and <string>; the next include reshuffle breaks them with non-local errors. <iostream> drags its static-init object and compile time into every TU that includes dun_parser.h for nothing. The `using json` alias is a public-namespace symbol leaked from two headers (harmless duplicate today, a collision waiting for any other `json` in mforce).

Recommendation: Add the five direct includes, drop <iostream>, and make the alias `namespace mforce::detail { using json = nlohmann::json; }` or spell nlohmann::json in signatures.

- verifier: Evidence at dun_parser.h:11 is real (`#include <iostream>`; zero std::cout/cerr/cin/clog in the file). The stated consequence does not follow: dun_parser.h is included by exactly ONE TU (tools/mforce_cli/main.cpp:15), which itself includes <iostream> at line 17, so there is no extra static-init object or compile cost anywhere. It does not 'fan out through templates.h' — templates.h (engine/include/mforce/music/templates.h:1-18) does not include it and nothing else does. '19 TUs' is wrong by any reading: only 7 of the 31 non-third_party .cpp files include anything under mforce/music/. Sub-claims checked: music_json.h std::unordered_map (117/153/174) and std::lround (244) are provided by its own DIRECT project includes basics.h (<unordered_map> line 4, <cmath> line 6) and structure.h (<unordered_map> line 10), not by nlohmann/<string> as claimed, so the 'next reshuffle breaks them' risk is overstated for those; parse_util.h snprintf (158) and std::string_view (272) genuinely lack direct <cstdio>/<string_view> and no project header in its chain supplies them — a real IWYU nit. `using json = nlohmann::json;` duplicates confirmed at templates_json.h:9 and music_json.h:13 (plus style_table.h:15, which the finding missed); identical alias-declarations in one namespace are legal and `json` is the alias convention at 14 sites across the repo, so the collision risk is contrived. Net: a bundle of style/IWYU nits with no concrete cost; the headline consequence is false.
- verifier: Evidence at the cited line is real: dun_parser.h:11 includes <iostream> and the file uses no cout/cerr/cin (only ifstream/istringstream from <fstream>/<sstream>). The stated consequence does not follow. (1) dun_parser.h is included by exactly one TU, tools/mforce_cli/main.cpp:15, and that TU already includes <iostream> itself at line 17 for its own std::cout/std::cerr, so dropping it changes nothing: zero static-init or compile-time cost. (2) The 'fan out through templates.h to 19 TUs' premise is false for every header named: templates.h includes basics/figures/figure_transforms/figure_constraints/structure/realization_strategy/voicing_profile + std headers, none of which include dun_parser.h, music_json.h, parse_util.h or templates_json.h. (3) 'compile today only via transitive includes from nlohmann/json.hpp and <string>' is wrong for music_json.h: <unordered_map> and <cmath> arrive through its direct include of basics.h (basics.h:4,6). The parse_util.h snprintf/<cstdio> and string_view/<string_view> points, and the duplicate `using json = nlohmann::json;` at music_json.h:13 / templates_json.h:9, are real but legal and cost-free today (identical alias redeclaration is well-formed; the collision is speculative). Net: a style/IWYU nit with no concrete cost, which per the review rules is low.

## F082 [CONFIRMED] medium (reporter: medium) soundness — engine/include/mforce/music/dun_parser.h:147

**DURN parse_token never verifies the token was fully consumed; trailing characters, misplaced dots and glued `|` separators are silently accepted**

Reporters: music-json

Evidence: `  Ornament ornament{};
  if (pos < int(tok.size())) {
    if (tok[pos] == '~') { ... }
    else if (pos + 1 < int(tok.size())) { ... recognised pairs only ... }
  }
  DunToken t{dur, step, isRest, accidental, tied};   // pos may be < tok.size() here, nothing checks`

Why: `qn.` (dot after the direction) parses as a plain quarter because the dotted check at 87 only looks immediately after the duration letter; `qu2x`, `qn|` and `qn/` parse as valid notes, and because `|`, `/` and `//` are only recognised as standalone tokens (264-268) a bar glued to a note silently loses the figure boundary. Downstream, dun_to_piece 336-338 maps any scale other than Minor/NaturalMinor to Major (`scale:Dorian` -> Major, no message) and the meter if-chain 341-349 maps unknown meters to 4/4; parse_header 169-199 ignores unknown keys and `meter:3` without a slash. std::stoi/stof exceptions at 183-198 and 275 surface as "invalid stoi" with no line context, unlike the hand-written throws. This is the corpus-ingest path (MTD/DURN work), where a silently wrong note is the expensive failure.

Recommendation: End parse_token with `if (pos != int(tok.size())) throw std::runtime_error("Trailing characters in DUN token: " + tok);`. Throw on unknown scaleName/meter in dun_to_piece and on unknown header keys. Wrap stoi/stof in a helper that rethrows with the token/line.

- verifier: Confirmed at dun_parser.h:147-164: parse_token constructs DunToken at 161 without ever checking pos == tok.size(). Traced each example: `qn.` -> dot check at 87 only looks immediately after the duration letter, '.' at pos 2 is skipped by both the accidental check (137-140) and the ornament check (147-159), result = plain quarter step 0. `qu2x` -> step +2, 'x' silently ignored. `qn|` / `qn/` -> parse_dun tokenises on whitespace (234) and matches `|`, `/`, `//` only as standalone tokens (264-268), so a glued bar becomes a valid note and the figure boundary is lost with no diagnostic. dun_to_piece scale fallback to Major (336-338) and meter fallback to 4/4 (341-349), parse_header silently dropping unknown keys and `meter:3` (175-186), and raw std::stoi/stof at 183-198 and 275 all as described. One correction to the framing: this is NOT the corpus-ingest path — corpus/ contains zero references to DUN/DURN (grep -i durn|dun), dun_parser.h is included only by tools/mforce_cli/main.cpp and exercised only by `--dun` on the two hand-authored scores/baselines/k467_*.dun files, and that path currently renders silence anyway (see F083), so today's practical cost is nil. Still a genuine silent-acceptance gap in a notation parser; medium (worth fixing when --dun is revived), not high.
- verifier: Traced parse_token by hand against the code at 61-164: after the ornament block (147-159) nothing compares pos to tok.size() before constructing DunToken at 161. Concrete cases verified: `qn.` -> dur 1.0 step 0 (the dotted check at 87 only looks right after the duration letter; the correct form is `q.n`, as used in the K467 file); `qu2x`, `qn|`, `qn/` all return valid tokens with the tail ignored, and `|`, `/`, `//` are matched only as whole tokens at 264/266/268, so a glued bar silently drops the figure boundary. dun_to_piece 336-338 collapses any scaleName other than Minor/NaturalMinor to Major; 341-349 default unknown meters to 4/4; parse_header 169-199 skips unknown keys and leaves `meter:3` (no slash) at the 4/4 default; std::stoi/stof at 183-198 and 275 throw std::invalid_argument whose what() is caught in main() (main.cpp:1242) and printed as 'ERROR: invalid stoi argument' with no token/line. All cited line numbers are accurate. One framing correction: DUN is NOT the corpus-ingest path. corpus/ and scripts/ contain no parse_dun/load_dun/.dun references; dun_parser.h's only consumer is mforce_cli --dun, and the only .dun files are the two hand-authored scores/baselines/k467_*.dun. The MTD work is Python-only. So the 'expensive silent failure in corpus ingest' consequence is overstated; the defect is real but its blast radius today is two hand-written baseline files through a CLI mode that is currently silent anyway (F083). Worth fixing if the parser is kept.

## F083 [CONFIRMED] high (reporter: high) soundness — engine/include/mforce/music/dun_parser.h:439

**Hand-built Pieces (DUN mode) have no realize step; `--dun` renders silence since the tree-walk was deleted**

Reporters: arch-music-model

Evidence: `dun_to_piece fills only the Passage tree: `phrase.add_melodic_figure(std::move(fig));` ... `part.passages[sectionName] = std::move(passage);` -- Conductor reads only events: conductor.h:560-563 `// Composer fully populates Part.elementSequence; that's all we read. if (!part.elementSequence.empty())` -- and the only Passage->Element realizer is private: composer.h:429 `void realize_event_sequences_(Piece& piece, ...)` called solely from compose() (237).`

Why: The realize step was moved into Composer (commit 4f25d8b deleted Conductor's tree-walk) but never exposed for Pieces that are built without a template. Confirmed: `mforce_cli --dun scores/baselines/k467_bars_1_to_27.dun patches/library/winds/oboe1.json <scratch>.wav` prints `Rendered: ... (104 beats @ 100 bpm, peak=0)` -- the K467 baseline the memory file calls 'renders end-to-end' is silent at HEAD. Architecturally this is the Composer<->Conductor boundary leaving a third owner (direct-build) with no path to the ElementSequence.

Recommendation: Make realization a public, template-independent operation: e.g. a free function `realize_events(Piece&)` (the body of realize_event_sequences_/realize_passage_to_events_ needs only Piece data) called by compose() and by run_dun; or delete --dun and the DUN parser if the feature is retired. Add a smoke test asserting peak > 0 for the DUN baseline.

- verifier: Confirmed structurally and empirically. Path: main.cpp:815 run_dun -> load_dun (832) -> dun_to_piece (841), which populates only part.passages (dun_parser.h:439 add_melodic_figure, 445 part.passages[sectionName]) and never touches elementSequence -> Conductor::perform (conductor.h:545-568) whose only per-part action is `if (!part.elementSequence.empty()) perform_events(...)` (561-562) under the comment at 560. The sole Passage->ElementSequence realizer is Composer::realize_event_sequences_ (composer.h:429, private), called only from compose() at 237; Part's direct builders (structure.h:248-272 add_note/add_chord/add_hit/add_rest) are manual and unused by the DUN path. Commit 4f25d8b (2026-04-22, 'refactor(conductor): delete tree-walk path entirely', conductor.h -118 lines) verified. Reproduced: ran build/tools/mforce_cli/Release/mforce_cli.exe --dun scores/baselines/k467_bars_1_to_27.dun patches/library/winds/oboe1.json <scratchpad>.wav -> 'Rendered: ... (104 beats @ 100 bpm, peak=0)', exit 0, no warning. Exe built 2026-09-22 20:35; HEAD 446fc3c (20:49) touched no engine headers or mforce_cli, working tree clean for engine/include/mforce/music and tools/mforce_cli, so this is HEAD behaviour. composer.h:416-417 still cites the deleted 'Conductor's tree-walk fallback' (stale comment). High: a shipped CLI mode writes a silent WAV with exit 0 against a tracked baseline score. Not critical only because --dun is a dev harness outside the active comp workflow (last DUN commit April).
- verifier: Reproduced at HEAD: ran build/tools/mforce_cli/Release/mforce_cli.exe --dun scores/baselines/k467_bars_1_to_27.dun patches/library/winds/oboe1.json <scratchpad>/k467_dun_check.wav. Output: 'Loaded DUN ... (2 sections, 7 phrases, key C Major)' then 'Rendered: ... (104 beats @ 100 bpm, peak=0)', exit code 0, 12.5 MB WAV of silence. Call chain confirmed: run_dun (main.cpp:815-900) does dun_to_piece then Conductor::perform(piece) directly with no Composer; dun_to_piece (dun_parser.h:326-451) only fills the Passage tree via add_melodic_figure/add_phrase/part.passages[...] and never references elementSequence (grep: zero hits in the file); Conductor::perform (conductor.h:560-563, lines accurate) only calls perform_events when part.elementSequence is non-empty; realize_event_sequences_ (composer.h:429) sits after the `private:` at composer.h:307 with no later `public:`, and its sole call site is compose() at composer.h:237. Every writer of elementSequence is either Part::add_note/add_chord/add_hit/add_rest (structure.h:249-271, direct-build primitives dun_to_piece does not use) or Composer's private realize_passage_to_events_ (composer.h:576) / realize_chord_parts_ (composer.h:731), so 'the only Passage->Element realizer is private' holds. Commit 4f25d8b (2026-04-22) is confirmed as 'refactor(conductor): delete tree-walk path entirely' (118 deletions in conductor.h); the K467 DUN baseline was finalized in ba82d0f on 2026-04-13, before that deletion, which matches the memory note's 'renders end-to-end' being pre-regression. No engine_tests coverage mentions DUN, and docs/autonomy has no entry flagging --dun as broken, so this is untracked. Severity: high, not critical. It is a confirmed correctness regression that reports success while emitting silence, but the mode is dormant (no docs/scripts reference it, only two baseline files exist) and nothing is lost or RT-unsafe.

## F084 [CONFIRMED] high (reporter: high) soundness — engine/include/mforce/music/figure_transforms.h:236

**split/retrograde_steps/add_neighbor/add_turn brace-init FigureUnit{dur, step} and silently drop rest, accidental, articulation and ornament**

Reporters: music-model

Evidence: `out.units.push_back({sub, src.step});      // first inherits step
      for (int k = 1; k < repeats; ++k)
        out.units.push_back({sub, 0});            // rest step=0`

Why: Same pattern at retrograde_steps (111, 113), add_neighbor (264-266), add_turn (290-293) and the inserted unit in vary_rhythm (316). Figures DO carry these fields: music_json.h:340-341 reads rest/accidental, dun_parser.h:405-434 sets rest/accidental/ornament, composer.h:1229 and 1737 set rest=true. elaborate() (449-498), which is what apply(TransformOp::Complexify) runs (563-568) and the composer uses for motif derivation, calls split() on any unit >= 2 beats, so a 2-beat rest becomes two sounding quarter notes; apply(Reverse)/templates.h:770 retrograde drops every ornament and rest. split_moving_ (418-421) does it right by copying `src` first, so the two split paths disagree.

Recommendation: In every transform construct the new unit as a copy of the source unit and then overwrite duration/step (as split_moving_ does); for retrograde decide explicitly whether per-unit marks travel with the pitch or the rhythm slot. Add a test with a rest + ornament figure through each transform.

- verifier: Evidence confirmed verbatim at figure_transforms.h:236-238. FigureUnit (figures.h:501-508) carries rest, accidental, articulation, ornament; the brace-init {dur, step} leaves all four at defaults. Same pattern confirmed at retrograde_steps 111/113, add_neighbor 264-266, add_turn 290-293, and the inserted unit in vary_rhythm 316; split_moving_ 418-421 copies src first, so the two split paths do disagree. Producers of those fields confirmed: music_json.h:340-345 from_json reads rest/accidental/articulation/ornament; dun_parser.h:405-434 sets rest/accidental/ornament; composer.h:1229 and 1737 set rest=true. Reachability of the consequence confirmed on production paths: composer.h:97-98 puts authored motif content (JSON, rests included) into realizedMotifs; derived motifs go through figure_transforms::apply at composer.h:129-130 and 1510-1513; FigureSource::Transform at composer.h:1248-1253 calls apply_transform -> apply. apply(Complexify) at 563-568 calls elaborate(), whose rung 1 (457-459) splits any unit >= 2 beats via split(), so a 2-beat rest unit becomes two rest=false quarter units. apply(Reverse) 514-515 and templates.h:770 both call retrograde_steps, which rebuilds every unit with only duration/step. No test in tools/test_figures exercises a rest or ornament through a transform. Silent musical-content corruption on the comp lane's main motif-derivation path; not RT-related, so high rather than critical.
- verifier: Evidence verified verbatim at figure_transforms.h:236-238. FigureUnit (figures.h:501-508) carries rest/accidental/articulation/ornament; brace-init {dur, step} leaves them at defaults. Same pattern confirmed at retrograde_steps 111/113, add_neighbor 264-266, add_turn 290-293, vary_rhythm 316 (FigureUnit newUnit{dur2, 0}). Field producers cited all exist: music_json.h:340-341 from_json reads rest/accidental/ornament; dun_parser.h:405-434 sets rest/accidental/ornament; composer.h:1229 and 1737 set rest=true for literal notes. elaborate() 457-459 splits any unit with duration >= 2.0 with no rest check, so a 2-beat rest does become two rest=false 1-beat units; apply(Complexify) 563-568 routes to elaborate; templates.h:770 add_derived_motif(Reverse) calls retrograde_steps which rebuilds every unit as {duration, step} and drops all marks. split_moving_ 418-421 copies src first, so the two split paths genuinely disagree. Live paths: composer.h:129 and 1510 realize derived motifs (JSON-loaded parents, which can carry rests) via apply(); composer.h:1248-1254 FigureSource::Transform; period_passage_strategy.h:256 Modified-variant consequents via add_derived_motif. Exposure today is limited (test_k467_period.json has rest-carrying motifs but no score currently sends a rest-carrying parent through Complexify/Reverse), but the silent conversion of rests to sounding notes in the core transform library is a real correctness flaw, hence high rather than critical.

## F085 [REFUTED] low (reporter: medium) workaround-hack — engine/include/mforce/music/figure_transforms.h:372

**Exceptions used as control flow inside complexify; smooth_voicing_selector swallows everything with catch(...)**

Reporters: arch-modern-cpp

Evidence: `372: `try {` ... 376: `} catch (const std::invalid_argument&) {` steering the transform retry path; smooth_voicing_selector.h:46 `} catch (...) {``

Why: Throw/catch in a per-figure generation loop costs microseconds per iteration and hides which precondition failed; catch(...) in the voicing selector masks real bugs (including bad_alloc) as 'no voicing'.

Recommendation: Return std::optional/expected from the inner helper and branch on it; narrow the catch in smooth_voicing_selector to the specific exception type and log it.

- verifier: Evidence exists: try at 372, catch(const std::invalid_argument&) at 376, and smooth_voicing_selector.h:46 catch(...). The stated consequence does not follow. (1) complexify() is not on any production path: the only caller in the repo is tools/test_figures/main.cpp:472; apply(TransformOp::Complexify) at 563-568 dispatches to elaborate(), which has no try/catch, and composer/strategies only reach the transforms via apply(). (2) Inside complexify the catch essentially never fires: addAt = rng.int_range(0, note_count-1) (randomizer.h:73-74 is inclusive) and split(..., 2)/add_neighbor/add_turn accept every addAt in [0, n-1]; the only way to throw is uniform_real_distribution<float> returning exactly 1.0f, so there is no per-iteration throw cost (a try region that never throws costs nothing). The 'costs microseconds per iteration in a per-figure generation loop' claim is therefore unfounded. (3) The catch(...) at smooth_voicing_selector.h:41-48 wraps ChordDictionary::get and get_chord_def, which throw std::runtime_error on an unknown dictionary/chord name (engine/src/chord.cpp:59-63, 147-152); on catch it leaves sc.quality as the canonic ChordDef and continues to resolve a voicing, so 'masks real bugs as no voicing' misdescribes the behavior. Narrowing that catch to std::runtime_error is a fair style nit, nothing more. Not a per-sample path, so no CLAUDE.md RT rule is implicated.
- verifier: Code exists as quoted (try at 372, catch(const std::invalid_argument&) at 376; smooth_voicing_selector.h:46 catch(...)), but the stated consequence does not follow. complexify() is not on any engine production path: the only caller is tools/test_figures/main.cpp:472; apply(TransformOp::Complexify) at 563-568 calls elaborate(), which has no try/catch. So there is no per-figure generation loop paying exception cost. Further, inside complexify addAt is drawn from rng.int_range(0, note_count()-1), which satisfies the preconditions of split(...,2), add_neighbor and add_turn, so the throw can never fire; the try block is zero-cost on the non-throwing path. The smooth_voicing_selector part misdescribes the behavior: the catch(...) wraps ChordDictionary::get / get_chord_def (both throw std::runtime_error on unknown names, engine/src/chord.cpp:147-151, 59-62) and falls back to the ScaleChord's existing quality pointer, not to 'no voicing'. Narrowing catch(...) to std::runtime_error is a legitimate nit, but as a 'exceptions as control flow with measurable cost' finding it is refuted.

## F086 [CONFIRMED] low (reporter: low) workaround-hack — engine/include/mforce/music/figure_transforms.h:560

**Dead expressions, silent no-op op, stale notes and mojibake comments scattered through the unit**

Reporters: music-model

Evidence: `case TransformOp::RhythmTail:
      return base;      // declared op silently returns the input; `default:` at 571-572 swallows unknown ops the same way`

Why: Locations: figure_transforms.h:308 `if (dur < 0.5f * 2.0f)`; :319 re-checks the loop condition `x < out.note_count() - 1`; figures.h:434 identical ternary branches; basics.h:165 unused `int d`; locus.h:13 forward-declares HarmonyTimeline after structure.h (included at line 2) already defined it; basics.h:257-260 describes an EqualTemperament class that does not exist; structure.h:90 `// remove with stage 9`; figures.h comment separators rendered as `???` on 21 lines (14, 51, 94, 216, 281, 284, 383, 467, 498, 511, 523, 531, 570, 577, 597, 607, 631, 647, 667, 711, 743 — em-dashes lost to a code-page save). RhythmTail in apply() returns the untouched figure although templates.h:789 implements it for motif derivation, so a FigureTemplate transform set to rhythm_tail renders the whole figure with no diagnostic.

Recommendation: Throw (or static-dispatch) for ops apply() does not implement; delete the dead expressions and stale comments; re-save figures.h as UTF-8 and restore the dashes.

- verifier: Primary evidence confirmed at figure_transforms.h:560-561 (RhythmTail returns base) and 570-572 (None/default return base). templates.h:789-802 implements RhythmTail as a PulseSequence derivation in add_derived_motif, so the two dispatchers disagree. Consequence confirmed: composer.h:1248-1253 routes FigureSource::Transform through apply_transform (default_strategies.h:159-162) -> apply, and derived motifs go through apply at composer.h:129 and 1510, so transform=rhythm_tail (parsed at templates_json.h:144) yields the untouched parent figure with no diagnostic. Side claims checked: :308 '0.5f * 2.0f' constant expression present; :319 re-checks the for-condition 'x < out.note_count() - 1' which cannot have changed in that branch (redundant); figures.h:434 'c.isTriplet ? c.duration : c.duration' identical branches present; basics.h:165 'int d = degree1;' is never read (166-173); basics.h:257-260 EqualTemperament comment with no such class anywhere in engine/ (only hit is the comment itself); structure.h:90 '// remove with stage 9' present; figures.h has exactly 21 lines containing '???'. One imprecision: locus.h:13 forward-declares HarmonyTimeline, which is defined in harmony_timeline.h:22, not structure.h — but structure.h:4 includes harmony_timeline.h, so the forward declaration is still redundant as claimed. Most items are nits; the silent RhythmTail no-op on a production template path is the one with concrete cost (wrong figure, no signal, one-line throw to fix), which lifts my call to medium.
- verifier: All locations verified. figure_transforms.h:560-561 RhythmTail returns base; 570-572 None/default return base. :308 `if (dur < 0.5f * 2.0f)` present; :319 `x < out.note_count() - 1` re-checks the unmodified loop condition (if-branch breaks after insert, so nothing mutates before the else-if). figures.h:434 `c.isTriplet ? c.duration : c.duration` identical branches. basics.h:165 `int d = degree1;` never referenced in semitones_between. locus.h:13 forward-declares HarmonyTimeline although locus.h:2 includes structure.h, which includes harmony_timeline.h (defines struct HarmonyTimeline at :22) — redundant, harmless. basics.h:257-260 comment describes an EqualTemperament class; grep finds only that comment. structure.h:90 `// remove with stage 9` present. figures.h has literal '???' bytes (od confirms three '?' chars, not a multibyte sequence) on exactly the 21 listed lines. templates.h:789-800 implements RhythmTail for add_derived_motif; templates_json.h:144 parses "rhythm_tail"; composer.h:1248-1254 FigureSource::Transform -> apply_transform -> apply, so a FigureTemplate with transform rhythm_tail silently gets the untouched base. No current score hits that path (the two rhythm_tail uses in test_k467_period.json are motifs with authored units, so realize_motifs skips them). Low severity: cleanup items with no concrete cost beyond the silent RhythmTail no-op.

## F087 [CONFIRMED] low (reporter: low) smell — engine/include/mforce/music/figures.h:14

**figures.h has encoding mojibake (em-dashes replaced by '???') and the repo has no .editorconfig/.gitattributes/.clang-format to prevent it**

Reporters: arch-build-headers

Evidence: `figures.h:14 `// PulseSequence ??? sequence of durations in beats.`, :51, :94, :383, :467, :498, :511, :522, :531, :570, :577, :596, :607, :631, :647, :667, :711, :743 all `???` where sibling headers use `—`; `ls -la | grep -i 'clang|editorconfig|CMakePresets|gitattributes'` at the repo root returns nothing; grep for `???` across engine/include matches only this file.`

Why: Shows a lossy encoding round-trip already happened once; without `.gitattributes` (`* text=auto eol=lf`, `*.h working-tree-encoding=UTF-8`) or `/utf-8` on MSVC (not in any CMakeLists), the next editor that opens a header with the system code page will corrupt the next file, and MSVC will interpret the sources as CP-1252 unless a BOM is present.

Recommendation: Fix the 18 comment lines; add `.gitattributes` with `* text=auto` and `.editorconfig` with `charset = utf-8`; add `/utf-8` to the MSVC compile options so source encoding is explicit.

- verifier: Confirmed: figures.h has literal ASCII '???' (od -c shows three 0x3F bytes) at 21 comment lines incl. 14/51/94/383/467/498/511/522/531/570/577/597/607/631/647/667/711/743; it is committed (git show HEAD: 21 hits), the only such file under engine/include, no BOM; sibling structure.h:123 uses a real UTF-8 em-dash. Repo root has no .gitattributes/.editorconfig/.clang-format (find returns nothing); no /utf-8 or /source-charset in any CMakeLists (only /W4 /permissive- /bigobj /Zi). Consequence (comments already lost once, no guard against recurrence) follows. Damage is comment-only, so cost is readability, not behavior.

## F088 [CONFIRMED] medium (reporter: medium) smell — engine/include/mforce/music/figures.h:110

**StepGenerator::random_sequence ignores its second parameter, so the preferSkips template knob is silently dead on the procedural path**

Reporters: music-model

Evidence: `StepSequence random_sequence(int length, float /*unused*/ = 0.3f) {`

Why: Callers still pass meaning: default_strategies.h:117-118 computes `skipProb = figTmpl.preferSkips ? 0.6f : 0.3f` and passes it; tools/mforce_cli/main.cpp:247-259 passes 0.0f. The authored `preferSkips` field has no effect and nothing says so.

Recommendation: Remove the parameter (the compile errors flush the stale call sites and the dead knob) or implement it in the interval-size roulette at 163-180.

- verifier: Confirmed at figures.h:110: second parameter is unnamed (`float /*unused*/ = 0.3f`) so it cannot be referenced in the body; interval roulette at 163-180 uses hard-coded 0.58/0.20/0.10. Callers pass meaning: default_strategies.h:117-118 computes skipProb from figTmpl.preferSkips and passes it; mforce_cli/main.cpp:247/253/259 pass 0.0f expecting 'stepwise'. preferSkips has no other consumer (only templates.h:119 field + templates_json.h:323/386 round-trip), and scores/baselines/template_binary.json:115 actually sets "skips": true, so an authored baseline knob is silently a no-op. Severity medium: silent dead template knob with a shipped baseline relying on it.

## F089 [CONFIRMED] low (reporter: low) smell — engine/include/mforce/music/figures.h:337

**Dead code and encoding damage in figures.h**

Reporters: arch-music-model

Evidence: `337-379 `StepSequence skip_sequence(int length, int startDegree)` -- no callers in engine/ or tools/ (flagged in docs/audits/2026-04-20-composition-framework-checkpoint.md:46, still present); 110 `StepSequence random_sequence(int length, float /*unused*/ = 0.3f)`; 434 `float compareVal = c.isTriplet ? c.duration : c.duration;` (both arms identical); 21 section headers read `// PulseSequence ??? sequence of durations` (14, 51, 94, 383, 498, 511, 522, 531, 570, 607, 631, 647, 667, 711, 743) where em-dashes were lost to a code-page save.`

Why: Dead generators invite reuse of untested paths; the no-op ternary suggests an abandoned triplet-weighting intent that silently does nothing; the mojibake makes the file's own section headers unreadable and will keep corrupting on round-trips through editors that honour the wrong encoding.

Recommendation: Delete skip_sequence and the unused parameter; either implement the triplet comparison or remove the ternary; re-save figures.h as UTF-8 and restore the headers.

- verifier: skip_sequence (figures.h:337-379) has no callers in engine/ or tools/ (grep hits only the definition and docs/audits); audit doc 2026-04-20 line 46 and 147 flag it, still present. Dead ternary at 434 confirmed verbatim (`c.isTriplet ? c.duration : c.duration`); the comment above says triplets should compare group total, which c.duration already is for triplets, so the ternary is pure no-op rather than a lost intent. Mojibake confirmed (see F087), though the count is slightly off: 20-21 lines, the listed 15 are a subset. Largely overlaps F087 (encoding) and F088 (unused param).

## F090 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/music/figures.h:399

**Binary duration alphabet, inverse-distance weighting and roulette selection are implemented three times (plus a fourth roulette in elaborate)**

Reporters: arch-build-headers, music-model

Evidence: `static const float BINARY[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};   // figures.h:399 and again 473
...
rhythm_util.h:27: static const float DURATIONS[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };`

Why: PulseGenerator::generate (399-449), generate_count (473-490) and rhythm_util.h generate_musical_rhythm (27-65, which has no callers anywhere) each carry the table, the `1/(1+|d-dp|*2)` weight (435, 477, rhythm_util.h:49) and the accumulate-until-pick loop (443-449, 483-489, rhythm_util.h:55-61); elaborate re-rolls the loop again at figure_transforms.h:477-483 even though Randomizer already exposes weighted selection (`rng.select_int({...},{...})`, figures.h:353). Also figures.h:434 `c.isTriplet ? c.duration : c.duration` is a dead ternary.

Recommendation: One `kBinaryDurations` table, one `weighted_index(Randomizer&, span<const float>)` on Randomizer, delete generate_musical_rhythm.

Also reported as: Small shared tables/formulas defined in multiple headers: the duration alphabet three times, ap1_phase_delay twice

- verifier: All three copies confirmed: BINARY table at figures.h:399 and :473, DURATIONS at rhythm_util.h:27; weight `1/(1+|d-dp|*2)` at 435, 477, rhythm_util.h:49; accumulate-until-pick loops at 443-449, 483-489, rhythm_util.h:55-61. generate_musical_rhythm has zero callers (only its definition and a doc comment at rhythm_util.h:10; shape_strategies.h:5 includes the header but never calls it). figure_transforms.h:478-483 is a fourth roulette (subtract-style). Randomizer::select_int(values, probs) at randomizer.h:91 is weighted value selection, not index selection, so 'already exposes weighted selection' is slightly overstated but usable. Not on any render path; offline composition only. Medium is defensible mainly for the dead rhythm_util function; the table/loop duplication itself is small.

## F091 [CONFIRMED] medium (reporter: medium) modern-cpp — engine/include/mforce/music/figures.h:533

**Figure uses a virtual hierarchy, clone() and dynamic_cast to carry what is a one-bit tag, forcing a hand-written deep copy on Phrase and losing the type in JSON**

Reporters: arch-music-model

Evidence: `figures.h:533-567 `struct Figure { std::vector<FigureUnit> units; virtual ~Figure() ... virtual std::unique_ptr<Figure> clone() const = 0; }`; MelodicFigure (579-587) and ChordFigure (613-621) have byte-identical constructor bodies; structure.h:126 `std::vector<std::unique_ptr<Figure>> figures;` with a manual copy ctor/assign (139-151: `for (const auto& f : other.figures) figures.push_back(f->clone());`); composer.h:505 `bool isChordFig = (dynamic_cast<const ChordFigure*>(phrase.figures[f].get()) != nullptr);`; conversions by copying units at 1894-1896 and 2060-2062; music_json.h:515-522 from_json(Phrase) always constructs MelodicFigure. templates.h:78/164 already declares `enum class StepMode { Scale, ChordTone }` on FigureTemplate, which nothing consumes.`

Why: Value semantics are the natural fit for this data model (every transform in figure_transforms.h is already by-value), yet Phrase is move-only-with-a-custom-copy, every Passage copy in the walk3 best-of-N search (composer.h:1374-1379, 1610-1623) heap-allocates one object per figure, and the realized type is unrecoverable from JSON. The audit of 2026-04-20 flagged StepMode as orphaned; it is the field that should replace the hierarchy.

Recommendation: Collapse to one value type `struct Figure { std::vector<FigureUnit> units; StepMode mode{Scale}; }`, `std::vector<Figure>` in Phrase (defaulted copy/move), `switch (fig.mode)` in realization, and a `stepMode` key in to_json/from_json(Phrase). Delete clone(), the Phrase copy ctor/assign, and the dynamic_casts.

- verifier: Evidence confirmed: Figure 533-567 with virtual clone; MelodicFigure/ChordFigure ctor bodies 579-587 vs 613-621 identical; structure.h:126 vector<unique_ptr<Figure>> with hand-written copy ctor/assign at ~139-151; composer.h:505 dynamic_cast<const ChordFigure*>; ChordFigure built by copying units at 1894-1896 and 2060-2062; music_json.h:504-521 to_json writes only units and from_json always makes MelodicFigure; templates.h:78/164 StepMode consumed only by templates_json.h (round-trip), audit line 46 flags it. The JSON type loss is a real round-trip defect: mforce_cli main.cpp:643 saves `json pieceJson = piece` and :690 reloads via pj.get<Piece>(), so a ChordFigure (chord-tone indices) comes back as MelodicFigure (scale steps) and realizes differently. REFUTED sub-claim: 'every Passage copy in the walk3 best-of-N (composer.h:1374-1379, 1610-1623) heap-allocates per figure' — attempts are emplace_back()'d and built in place, phrases are add_phrase(std::move(phrase)) at 1592, and the winner is returned via std::move(attempts[win].passage) at 1636; no Passage copy occurs there. Per-figure heap allocation comes from make_unique at construction regardless, and this is offline composition, not a render loop, so no CLAUDE.md RT rule is implicated. Same finding as F092.

## F092 [CONFIRMED] low (reporter: medium) elegance — engine/include/mforce/music/figures.h:574

**Figure/MelodicFigure/ChordFigure polymorphism exists only to be recovered by dynamic_cast; it costs a unique_ptr per figure, virtual clone and a hand-written deep copy in Phrase**

Reporters: music-model

Evidence: `MelodicFigure(const PulseSequence& pulses, const StepSequence& steps) { ... }   // 579-587
...
ChordFigure(const PulseSequence& pulses, const StepSequence& steps) { ... }    // 613-621, identical body
...
composer.h:505: bool isChordFig = (dynamic_cast<const ChordFigure*>(phrase.figures[f].get()) != nullptr);`

Why: The two subclasses are byte-identical (constructor 579-587 vs 613-621, clone 589-593 vs 623-627); the only behavioural difference is the step semantics the composer selects by dynamic_cast. The hierarchy forces `std::vector<std::unique_ptr<Figure>>` and the custom copy ctor/assignment in structure.h:136-151, plus RTTI checks, for a tag's worth of information.

Recommendation: Collapse to one `Figure` value type with `enum class FigureKind { Melodic, Chord }`; Phrase holds `std::vector<Figure>`, delete clone() and the custom copy ops, replace the dynamic_cast with `fig.kind == FigureKind::Chord`.

- verifier: Duplicate of F091 with narrower, fully confirmed evidence: identical ctor bodies (579-587 vs 613-621) and clone bodies (589-593 vs 623-627); dynamic_cast at composer.h:505 is the only engine-side discriminator (tools/test_figures/main.cpp has 5 more casts to MelodicFigure); structure.h ~136-151 custom copy ops exist because of unique_ptr. Consequence (a one-bit tag carried via RTTI + virtual clone + hand-written deep copy) follows. No hot-path cost; offline composer only. Would be medium only in combination with the JSON type-loss noted under F091; on its own it is a style/elegance item.

## F093 [UNVERIFIED] medium (reporter: medium) build — engine/include/mforce/music/locus.h:3

**locus.h, piece_utils.h and rhythm_util.h pull templates.h (and with it nlohmann/json, figure_transforms, realization_strategy, voicing_profile) into the model layer**

Reporters: music-model

Evidence: `#include "mforce/music/templates.h"   // locus.h:3; templates.h:9 is #include <nlohmann/json.hpp>
...
rhythm_util.h:2: #include "mforce/music/templates.h"   // only for enum FigureDirection`

Why: Locus only stores a `PieceTemplate*` and rhythm_util only needs the FigureDirection enum, yet every TU that touches a Locus or piece_utils parses json.hpp and the whole template/strategy stack. It also inverts layering: the structural model depends on the template/JSON layer, which is what makes the Phrase/Passage 'should move out of structure.h' note (structure.h:117-120) hard to act on.

Recommendation: Forward-declare `struct PieceTemplate;` in locus.h; move FigureDirection to a tiny header (or figure_constraints.h); include templates.h only in the .h/.cpp that dereference templates.


## F094 [REFUTED] low (reporter: medium) modern-cpp — engine/include/mforce/music/music_json.h:13

**Namespace-scope `using json = nlohmann::json;` in public headers**

Reporters: arch-modern-cpp

Evidence: `music_json.h:13 `using json = nlohmann::json;`; templates_json.h:9 same; style_table.h:15 same (all inside namespace mforce)`

Why: Every includer of mforce/music gets a global-looking `json` identifier injected into namespace mforce, which collides with any other json type and makes the dependency on nlohmann part of the public interface.

Recommendation: Qualify as nlohmann::json in headers (or a project-level `mforce::Json` alias in one deliberate header); keep `using json` to .cpp files.

- verifier: Evidence confirmed: `using json = nlohmann::json;` at music_json.h:13, templates_json.h:9, style_table.h:15, all inside namespace mforce. Consequence does not follow: there is no other json type anywhere in engine/ or tools/ (grep for struct/class json/Json finds nothing); the other aliases (patch_loader.cpp:42, explore.cpp:54, ppl_to_json/main.cpp:22) are global-scope, and mforce_ui's are function-local, so no collision or ambiguity exists; redeclaring an identical alias in the same scope is legal C++ anyway. 'Makes nlohmann part of the public interface' is already true via the `to_json(json&, ...)` signatures and the #include, not caused by the alias. Style preference with no concrete cost.

## F095 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/music/music_json.h:19

**Eleven hand-written two-direction enum<->string tables**

Reporters: arch-duplication

Evidence: `music_json.h Articulation 19-67, Dynamic 147-159, PitchSelectionType 162-187; templates_json.h FigureSource 95-112, TransformOp 114-147, MotifRole 150-171, MotifOrigin 173-188, PartRole 190-209, MelodicFunction 215-231, FigureShape 237-277, FigureDirection 283-308 — each a `to_string` switch plus a reverse if-chain.`

Why: Every enum value is spelled twice; the reverse chains silently default on a miss (the 2026-04-20 audit already flagged silent defaults), and the two directions can disagree.

Recommendation: One `template<class E> struct EnumTable { std::pair<E,const char*> rows[]; to_string(); from_string(); }` with a single row list per enum and a loud miss.

- verifier: All eleven tables exist at the cited ranges and each spells every enum value in both directions. Overstated in two ways: (1) Dynamic (147-159) and PitchSelectionType (162-187) are a names[] array + unordered_map, not switch + if-chain, and Articulation is a variant, not an enum; (2) 'silently default on a miss' is true for 7 of 11 (Articulation->Default, FigureSource->Generate, TransformOp->None, PartRole->Melody, MelodicFunction->Free, FigureShape->Free, FigureDirection->Ascending) but FALSE for Dynamic, PitchSelectionType (map.at throws out_of_range), MotifRole and MotifOrigin (explicit throw). No actual to/from disagreement found on inspection. Duplication is real and the 7 silent defaults are a genuine hazard (a typo in a template's shape string becomes Free with no error).

## F096 [CONFIRMED] low (reporter: low) smell — engine/include/mforce/music/music_json.h:368

**from_json loaders are split between 'clear then fill' and 'append to whatever is there' with no rule**

Reporters: music-json

Evidence: `inline void from_json(const json& j, MelodicFigure& f) {
  for (auto& uj : j.at("units")) { ... f.units.push_back(u); }   // no clear
}
// same: Phrase 515-522, Passage 530-548, ChordRealization 405-411, Part 571-594, Piece 600-612; templates_json.h SectionTemplate keyContexts 1216-1229
// vs clear-first: PulseSequence 351-354, StepSequence 359-362, ChordProgression 310-319, DrumFigure 627-635; templates_json.h PhraseTemplate 693, PassageTemplate 983, PieceTemplate 1258/1265/1275; PeriodSpec resets whole object at 791`

Why: Through `j.get<T>()` the target is fresh so both work, but these headers overwhelmingly call `from_json(j, existing)` directly, and the append loaders double content if the target was ever non-empty. Only from_json(PeriodSpec) (`ps = PeriodSpec{};`) does the obviously correct thing.

Recommendation: Adopt one convention: every from_json(T&) begins with `t = T{};` (or clears each container it fills). Cheap, mechanical, removes a class of surprise.

- verifier: Evidence confirmed: MelodicFigure 368-374, Phrase 515-522, Passage 530-548, ChordRealization 405-411, Part 571-594, Piece 600-612 append without clearing; PulseSequence 351-354, StepSequence 359-362, ChordProgression 310-312, DrumFigure 628 clear first; templates_json.h PhraseTemplate 693, PassageTemplate 983, PieceTemplate 1258/1265/1275 clear, PeriodSpec 791 resets, SectionTemplate keyContexts 1216-1229 appends. The inconsistency is real. Consequence is latent only: every direct from_json(j, existing) call in the headers and in tools (engine_tests 1284/1512, mforce_cli 530/1185, test_figures 889/1040) targets a freshly declared local, and the only Piece load is pj.get<Piece>() at mforce_cli:690. No caller reuses a non-empty target today. Note Part.passages[key]= and PartTemplate.passages[secName]= are map assignments, which replace rather than double.

## F097 [REFUTED] low (reporter: low) soundness — engine/include/mforce/music/music_json.h:368

**Several from_json overloads append into the target without clearing it, unlike their siblings**

Reporters: arch-build-headers

Evidence: `music_json.h:368-374 `inline void from_json(const json& j, MelodicFigure& f) { for (auto& uj : j.at("units")) { ... f.units.push_back(u); } }`; :515-522 from_json(Phrase) `ph.add_melodic_figure(std::move(f));` with no clear; :530-535 from_json(Passage) `p.phrases.push_back(...)`; templates_json.h:1163-1173 from_json(PartTemplate) inserts into `pt.passages` without clearing. By contrast templates_json.h:693 `pt.figures.clear();`, :983 `pt.phrases.clear();`, music_json.h:311-312 `cp.chords.chords.clear(); cp.pulses.pulses.clear();``

Why: nlohmann's `j.get<T>()` constructs fresh objects so the common path is fine, but `j.get_to(existing)` or re-loading into a reused template (the UI's edit/reload loop) silently doubles units/phrases/passages with no error.

Recommendation: Clear the target container at the top of each from_json (the convention the other overloads already follow).

- verifier: Same evidence as F096 (append-without-clear confirmed at 368-374, 515-522, 530-535) but the stated consequence is unsupported: the 'UI edit/reload loop' does not exist -- tools/mforce_ui/main.cpp never loads Piece/Passage/Phrase/PieceTemplate JSON (grep shows only .psg text passages and settings), and no caller anywhere uses get_to() or re-loads into a reused object. The templates_json.h:1163-1173 PartTemplate claim is also mischaracterized: `pt.passages[secName] = std::move(pass)` is a map assignment that replaces an existing key, so it cannot 'double' passages. Treat as a duplicate of F096 with a fabricated failure path.

## F098 [CONFIRMED] medium (reporter: medium) soundness — engine/include/mforce/music/music_json.h:504

**Round-trip asymmetries: Phrase loses connectors and figure type; Section loses harmony and truncation; PieceTemplate::defaultPulse and SectionTemplate::styleName are one-directional**

Reporters: arch-music-model, music-json

Evidence: `(a) to_json(Phrase) 504-513 writes only startingPitch + figures[].units; from_json 515-522 rebuilds MelodicFigure only -- `connectors` (structure.h:133) never serialized. (b) to_json(Section) 550-553 `j = json{{"name",...},{"beats",...},{"tempo",...},{"meter",...},{"scale",...}}` -- keyContexts, chordProgression, harmonyTimeline, truncateTailBeats dropped, yet Conductor::perform reads truncateTailBeats (conductor.h:553). (c) templates.h:654 `float defaultPulse{0.0f};` on PieceTemplate is neither written nor read in templates_json.h:1236-1289, while composer.h:63 (`float sharedPulse = tmpl.defaultPulse;`) and 1494-1495 consume it. (d) SectionTemplate.styleName read at 1231-1233, absent from to_json 1179-1198.`

Why: A Piece saved then `--play`ed loses its truncation and its harmony; a Passage saved loses the inter-figure leadSteps so re-realization changes pitches; the walk1 spec's 'template's defaultPulse, falling back to 1 beat' knob is unreachable from any JSON file (so realize_motifs always draws the shared pulse from the RNG); a template with styleName cannot be re-emitted by --lint-template. These are the exact failure class the chordConfig fix at templates_json.h:871-873 describes.

Recommendation: Serialize connectors and figure mode on Phrase; serialize keyContexts/harmonyTimeline/truncateTailBeats on Section; add defaultPulse and styleName; add an engine_tests round-trip (to_json -> from_json -> to_json equality) over scores/baselines so asymmetries fail CI.

Also reported as: to_json(Phrase) drops Phrase::connectors and the Figure subtype; to_json(Section) drops truncateTailBeats and keyContexts — score round trip is lossy

- verifier: All four evidence items confirmed at the cited lines: (a) to_json(Phrase) 504-513 writes only startingPitch + units, from_json 515-522 rebuilds MelodicFigure only; connectors (structure.h:133) and ChordFigure subtype (figures.h:610) never serialized. (b) to_json(Section) 550-553 omits keyContexts/chordProgression/harmonyTimeline/truncateTailBeats (structure.h:207-216). (c) PieceTemplate::defaultPulse (templates.h:654) absent from templates_json.h:1236-1289 while composer.h:63 and 1494-1495 consume it; no baseline score sets it at top level, so the walk1-spec knob is genuinely unreachable from JSON. (d) styleName read at 1231-1233, absent from to_json 1179-1198, and consumed at composer.h:873 / period_passage_strategy.h:340. HOWEVER the (a)/(b) consequences are overstated: Conductor::perform reads only Part.elementSequence (conductor.h:560) and never harmony, so a --play'ed Piece sounds identical minus nothing; truncateTailBeats is only ever set by the DUN path (dun_parser.h:376), which performs directly and never saves JSON, while the compose->save->--play cycle has no SectionTemplate field that could set it, so 'loses its truncation' is not reachable; realize_event_sequences_ runs only inside compose() on fresh phrases, so 're-realization changes pitches' from a loaded Passage is not an existing path. (c) and (d) carry the concrete cost; (a)/(b) are data-model lossiness with no current behavioral effect.

## F099 [CONFIRMED] low (reporter: low) soundness — engine/include/mforce/music/parse_util.h:115

**Dead branch with wrong error message in parse_chord_string; parse_duration treats any non-dot second char as x1.25**

Reporters: music-json

Evidence: `} else {
    throw std::runtime_error("Chord token missing duration (use Root_dur or Root_group_dur): " + token ...);   // 115-117 fires for size 1 AND size > 3
}
if (parts.size() > 3)
    throw std::runtime_error("Too many '_' segments in chord token: " ...);   // 119-120 unreachable
--- 30-32 ---
if (durStr.size() >= 2) { beats *= (durStr[1] == '.') ? 1.5f : 1.25f; }`

Why: A four-segment token reports "missing duration" and the intended "too many segments" message can never fire. In parse_duration, `qq`, `q,` or `q5` all silently become 1.25 beats (the comment at 17 documents it, which does not make a typo channel a feature). std::stof at 179/393/416 and std::stoi at 360/394 throw library messages with no token number, unlike the hand-written throws beside them.

Recommendation: Reorder: check `parts.size() > 3` before the 2/3 dispatch. Make the second duration char an explicit set (`.` = 1.5, a named char for 1.25, anything else throws). Wrap stof/stoi in a helper that adds the token number.

- verifier: Confirmed at parse_util.h:109-120. parts.size()==1 and parts.size()>3 both fall into the else at 115-117 ("missing duration"), so the >3 check at 119-120 is unreachable and a 4-segment token gets the wrong message. parse_duration 30-32: any second char other than '.' multiplies by 1.25 (header comment line 17 documents it, but 'qq'/'q5' silently become 1.25 beats). std::stof at 179/393/416 and std::stoi at 360/394 confirmed; they throw std::invalid_argument/out_of_range without the token number the neighbouring hand-written throws carry. All line numbers accurate.

## F100 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/music/parse_util.h:174

**Note-name parsing exists in two forms inside parse_util.h and in four more places across dun_parser and durn_converter**

Reporters: arch-duplication

Evidence: `parse_util.h note tables 183-191 vs 249-257 and parse logic 174-219 vs 297-317; dun_parser.h duration switch 76-84 vs parse_util.h parse_duration 19-34; letter→semitone in durn_converter kern_parser.h:141-152, abc_parser.h:21-32, musicxml_parser.h:238-247, main.cpp 533-537/372/456-457; accidental calculation main.cpp 393-407 vs 417-430; scale tables in the parsers vs engine music.cpp:50-72.`

Why: Six parsers of the same pitch syntax accept different inputs; the comp corpus pipeline (durn_converter) and the UI transport (parse_note_input) can disagree on the same string, and durn_converter already includes engine/include so sharing costs nothing.

Recommendation: One `note_letter_semitone()`/`parse_note_name()` in music/basics.h or parse_util.h; one `parse_duration`; durn_converter parsers call them.

- verifier: All cited locations exist: parse_util.h noteMap 183-191 vs letterSemitone 249-257 (identical arrays, identical accidental/wrap blocks 201-210 vs 306-315); kern_parser.h letter_to_pc 141-152, abc_parser.h note_name_to_semitone 21-32, musicxml_parser.h midi_note switch 238-247 all encode C=0,D=2,E=4,F=5,G=7,A=9,B=11; durn_converter main.cpp 393-407 vs 417-430 is a verbatim copy of the accidental computation; scale step tables at kern_parser.h:68 and abc_parser.h:70 duplicate music.cpp:50-72; dun_parser.h 76-84 duration switch overlaps parse_duration (s/e/q/h/w same values) but is a different grammar (3-prefix triplet, no t/d/f, no 1.25 rule). durn_converter does include engine/include (tools/durn_converter/CMakeLists.txt:4). Caveats: main.cpp 372/456-457 are pc->name tables (reverse direction, not parsing) and 533-537 is a name->pc map with accidentals, so these are related but not the same code; the stated consequence that durn_converter and parse_note_input 'can disagree on the same string' is overstated since they parse different input formats (kern/abc/xml vs MForce passage strings) and never see the same string. Duplication itself is real; maintenance cost, not a correctness bug.

## F101 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/music/parse_util.h:183

**Note-letter parsing duplicated between parse_note_input and parse_passage; DunToken->FigureUnit copy repeated three times in dun_to_piece; two identical drum structs**

Reporters: music-json

Evidence: `static const int noteMap[] = { 9, 11, 0, 2, 4, 5, 7 };   // parse_util.h:183-191
... if (str[pos] == '#') { semitone += 1; pos++; } else if (str[pos] == 'b') { semitone -= 1; pos++; }  semitone = ((semitone % 12) + 12) % 12;   // 201-210
static const int letterSemitone[] = { 9, 11, 0, 2, 4, 5, 7 };   // parse_util.h:249-257, same accidental/wrap block at 306-315
--- dun_parser.h ---
u.duration = figDun[0].duration; u.step = 0; u.rest = figDun[0].rest; u.accidental = ...; u.ornament = ...;   // 402-408, again 416-422, again 429-435
--- parse_util.h ---
struct ParsedDrumPattern { DrumFigure figure; int repeats{1}; };   // 364-367
struct DrumToken        { DrumFigure figure; int repeats{1}; };   // 371-374`

Why: Three copies of the same semitone table/accidental logic across the two parsers (parse_util.h 183-210, 249-315) and three copies of the five-field unit copy in dun_to_piece (both branches of `if (fi == 0)` at 395-424 are the same code with `startIdx = 1` on each side, differing only in the step value). Any change to accidental handling or a new FigureUnit field must be made in every copy; the dun_to_piece triplication is where a new unit field (e.g. articulation) would be missed.

Recommendation: `parse_pitch_class(std::string_view, size_t& pos) -> int` shared by both parsers; `FigureUnit unit_from_token(const DunToken&, int step)` in dun_parser.h and collapse the fi==0/else branches to computing `step` only. Merge DrumToken into ParsedDrumPattern.

- verifier: Confirmed. parse_util.h 183-191 and 249-257 are identical 7-entry arrays with identical accidental/wrap logic (201-210 vs 306-315); the finding says 'three copies' but there are two in parse_util.h. dun_parser.h dun_to_piece: the five-field FigureUnit copy appears verbatim at 402-408, 416-422, 429-435, and the fi==0/else branches (395-424) both end with startIdx=1 and differ only in u.step and reader handling. ParsedDrumPattern (364-367) and DrumToken (371-374) have identical members; DrumToken is used only inside parse_util.h (parse_single_drum_token), ParsedDrumPattern is consumed by tools/mforce_ui/main.cpp:8202. A new FigureUnit field would need three edits in dun_to_piece. Medium is fair as a maintenance trap; no runtime consequence.

## F102 [CONFIRMED] low (reporter: low) modern-cpp — engine/include/mforce/music/parse_util.h:306

**C-style casts in music, tools and test harnesses**

Reporters: arch-modern-cpp

Evidence: `parse_util.h:306 `if (pos < (int)token.size() && token[pos] == '#')`, 309, 320; chord_walker.h:117 `for (int i = 0; i < (int)attackBeats.size(); ++i)`, 119, 180, 249, 312; segment_source.h:250 `const long long target = (long long)std::llround(accExact);` (llround already returns long long); mforce_cli/main.cpp:773 `(size_t)p.frames`; build_stamp.h:112 `reinterpret_cast<const wchar_t*>(raw.data() + 2)`; stk_ref/*.cpp `(int16_t)std::lrint(s * 32767.0)`, `(uint32_t)samples.size()``

Why: C casts hide narrowing and pointer reinterpretation; the engine proper uses static_cast/functional casts consistently, so this is inconsistency rather than danger.

Recommendation: Use static_cast/std::ssize (C++20) for size comparisons; drop the redundant llround cast.

- verifier: Every cited cast exists: parse_util.h 306/309/320 (int)token.size(); chord_walker.h 117/119/180/249/312; segment_source.h:250 (long long)std::llround (redundant, llround returns long long); mforce_cli/main.cpp:773 (size_t)p.frames; stk_ref/*.cpp (int16_t)std::lrint and (uint32_t)samples.size() in saxofony/mesh2d/flute/clarinet/brass refs. One evidence item is wrong: build_stamp.h:112 is already a reinterpret_cast (the C casts there are on lines 111, 113, 115). The 'why' premise is also false: the engine proper does NOT use static_cast consistently -- grep finds 58 C-style casts across 11 engine files (piece_utils.h 22, composer.h 10, chord_walker.h 5, chord.cpp 3, parse_util.h 3, fft.h, segment_source.h, etc.), so this is a repo-wide style pattern, not a tools/test-harness inconsistency. All instances are int/size narrowing in non-hot code with no concrete cost; style-only.

## F103 [REFUTED] low (reporter: low) soundness — engine/include/mforce/music/passage_melody.h:113

**Exact float equality on an accumulated beat cursor decides barline figure boundaries**

Reporters: arch-modern-cpp

Evidence: `110: `const float beatInBar = std::fmod(passageBeat, beatsPerBar);` 113: `} else if (beatInBar == 0.0f && !fig.units.empty()) {` with 128 `passageBeat += n.durationSeconds;``

Why: Durations that are not exactly representable (triplets 1/3, 2/3) accumulate rounding error, so a barline lands at 3.9999998 and the figure boundary is silently skipped, merging two figures.

Recommendation: Compare with a tolerance (`std::fabs(beatInBar) < 1e-4f || std::fabs(beatInBar - beatsPerBar) < 1e-4f`) or track beats in integer ticks.

- verifier: Evidence is accurate: passage_melody.h:110 `std::fmod(passageBeat, beatsPerBar)`, :113 `beatInBar == 0.0f`, :128 `passageBeat += n.durationSeconds`. The consequence does not follow for this code path. phrases_from_passage (line 64) calls parse_passage (parse_util.h:246) with bpm hardwired to 60; parse_passage's duration grammar is parse_duration (parse_util.h:19-34), which yields only {0.125,0.25,0.5,1,2,4,8,16} times {1, 1.5, 1.25} -- all dyadic rationals, exactly representable in float. durationSeconds = beats*60.0f/60.0f is exact (beats*60 fits in 24 bits for every grammar value, and the quotient is exactly representable so IEEE division returns it exactly). Sums of such dyadic values stay exact far beyond any realistic passage length, beatsPerBar is float(int) (passage_melody.h:141), and fmod of exact operands is exact, so beatInBar == 0.0f is a reliable barline test here. Triplet durations (1/3, 2/3) that the finding relies on exist only in dun_parser.h:66-94 (the `3` prefix), a separate parser never reached from phrases_from_passage; no caller feeds non-dyadic durations into this function (callers: apply_passage_melodies and engine_tests main.cpp:1202/1255/1261). The 'silently merged figures' failure cannot occur with the current grammar. Exact float equality is a latent brittleness if the .psg grammar ever adds triplets, which is worth a comment or tolerance, but that is a style/future-proofing note, not a present defect.

## F104 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/music/piece_utils.h:58

**Pitch has no valid/invalid state; piece_utils uses Pitch{} (null pitchDef) as a not-found sentinel and feeds it to PitchReader::set_pitch**

Reporters: music-model

Evidence: `Pitch seed = passTmpl.startingPitch ? *passTmpl.startingPitch
                 : (pass->phrases.empty() ? Pitch{} : pass->phrases[0].startingPitch);
    PitchReader pr(locus.piece->sections[locus.sectionIdx].scale);
    pr.set_pitch(seed);`

Why: basics.h:116 `const PitchDef* pitchDef;` has no initializer and note_number() (basics.h:119) dereferences unconditionally; set_pitch (pitch_reader.h:37,40) dereferences on entry. pitch_before also returns Pitch{} at 38, 39, 46 and 92, and callers immediately set_pitch it (composer.h:1656 then 1665; range_in_* seed the same way at 175-177). A passage with no template startingPitch and no realized phrase yet crashes with a null deref instead of a diagnostic. `Pitch p;` (default-init) is indeterminate, so PitchRange's lowest/highest are too.

Recommendation: Default `pitchDef{nullptr}`, add `bool valid() const`, assert in set_pitch/note_number, and have pitch_before return std::optional<Pitch> (or throw with the locus) instead of a sentinel.


## F105 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/music/piece_utils.h:67

**Cursor walk (leadStep then units) and template/seed lookup are copied four and two times across piece_utils**

Reporters: music-model

Evidence: `for (int fi = 0; fi < (int)ph.figures.size(); ++fi) {
        if (fi < (int)ph.connectors.size()) pr.step(ph.connectors[fi].leadStep);
        for (const auto& unit : ph.figures[fi]->units) {
          pr.step(unit.step);`

Why: The same walk appears at 67-76, 81-86, 152-155, 180-186, 189-193 and 214-219; the PassageTemplate lookup + seed computation at 41-47/58-59 is repeated at 167-171/175-176. The FC.leadStep rule already changed once (comment at 64-66); six copies is six places to miss next time.

Recommendation: One `walk_before(const Locus&, Visitor)` that resolves passage/template/seed and visits (figure, unit, reader) up to the locus; pitch_before and the three range_* functions become one-line visitors.


## F106 [CONFIRMED] low (reporter: medium) smell — engine/include/mforce/music/pitch_reader.h:61

**Dead comp-lane API families: chromatic passing tones, piece history queries, HarmonyTimeline::slice, Pitch::relative, Genre, RhythmicFigure, SimpleNote/DrumHit, drum perform, core/sequence.h**

Reporters: arch-duplication

Evidence: `No callers anywhere in engine/ or tools/ (tree grep, third_party excluded) for: PitchReader::chromatic_step/snap_to_degree/has_passing_tone_up/down (pitch_reader.h:61-79) and Scale::semitones_between/get_passing_tone (basics.h:162, 198; `int d = degree1;` at 165 unused); rhythm_util.h generate_musical_rhythm (25); piece_utils.h reader_before 98, range_in_phrase_before 140, range_in_piece_before 202 (range_in_passage_before 162 is called only by the dead 202); harmony_timeline.h `ChordProgression slice(...)` 40; basics.h:127 `static Pitch relative(...)` defined at music.cpp:44; compose.h `struct Genre` 35; figures.h `struct RhythmicFigure` 633; basics.h SimpleNote 280 and DrumHit 301; conductor.h `void perform(const Part& part, float bpm, DrumKit& kit)` 576 (the UI drives DrumKit via play_hit at main.cpp:8254); core/sequence.h (Sequence/SimpleSequence/CompositeSequence) is included by nothing.`

Why: These were added as "additive, no consumer yet" (the 2026-04-15 specs say so for the history queries) and never consumed; they enlarge the surface a reader must understand and some (RhythmicFigure, Genre) are named in CLAUDE.md priorities as if live. Dead inline code is also untested code.

Recommendation: Delete them (git keeps the history); if a specific one is wanted for the Markov/phrase work, reintroduce it with its first consumer.

- verifier: Evidence verified symbol by symbol via tree grep over engine/ and tools/ (third_party excluded; no tests/ dir exists). PitchReader::chromatic_step/snap_to_degree/has_passing_tone_up/down are at pitch_reader.h:61-79 with zero callers. Scale::semitones_between (basics.h:162) and get_passing_tone (198) have no callers; `int d = degree1;` at 165 is genuinely unused. rhythm_util.h:25 generate_musical_rhythm: no callers (shape_strategies.h includes the header but doesn't call it). piece_utils.h reader_before (98), range_in_phrase_before (140), range_in_piece_before (202): no callers; range_in_passage_before (162) is called only from 223 inside the dead range_in_piece_before. harmony_timeline.h:40 slice(): no `.slice(` callers. Pitch::relative declared basics.h:127, defined music.cpp:44, never called. compose.h:35 struct Genre: only other hits are comments in melody_profile.h/note_map.h. figures.h:633 RhythmicFigure, basics.h:280 SimpleNote, 301 DrumHit: no uses outside their definitions (UI drum loop at main.cpp:8254 iterates DrumFigure::Hit, not DrumHit). conductor.h:576 perform(Part,float,DrumKit&): the only perform(part,bpm,x) callers (cli 95/977, ui 8127) pass *ip.instrument which is unique_ptr<PitchedInstrument>, so the DrumKit overload is unreached (drum rendering goes through perform_events -> conductor.h:294 and the UI's direct play_hit). core/sequence.h is included by nothing. One inaccuracy in the 'why': CLAUDE.md names RhythmicFigure (priority 1) but does NOT name Genre. Consequence (unused surface, untested inline code) follows. Caveat on the recommendation: blanket deletion of RhythmicFigure conflicts with CLAUDE.md priority #1 which cites it as a JSON-format target; that item should be kept or discussed, not deleted.

## F107 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/music/pitch_walker.h:59

**Two scale walkers (PitchReader::step and step_note) with divergent descending semantics and a copy-pasted degree search**

Reporters: music-model

Evidence: `int prevDeg = (deg - 1 + scale.length()) % scale.length();
            nn -= scale.ascending_step(prevDeg);   // step_note descends on ascSteps
...
pitch_reader.h:104:  noteNumber -= scale.descending_step(degree);   // PitchReader descends on descSteps`

Why: For scales whose asc != desc (music.cpp:69-71 Harmonic Minor, Melodic Minor, Enigmatic) the two walkers produce different pitches for the same step sequence: the composer realizes with step_note (composer.h:549) while piece_utils cursor/range queries use PitchReader, so pitch_before() can disagree with what was realized. Inside PitchReader, step_up (asc) then step_down (desc) leaves noteNumber inconsistent with (octave, degree); the next set_octave/set_degree/snap_to_degree recomputes from asc only (107-112) and the pitch jumps. step_note also duplicates its degree-search verbatim in both branches (46-56 vs 63-68) and silently falls back to deg=0 for off-scale input.

Recommendation: Keep one walker. Implement step_note/snap_to_scale on PitchReader (or vice versa), decide the descending-form policy once (asc-only is what every other consumer assumes), and factor the degree search into a single helper.

- verifier: Evidence verified at the cited lines. pitch_walker.h:69-70 descends with scale.ascending_step(prevDeg); pitch_reader.h:104 (step_down) descends with scale.descending_step(degree). ScaleDef is {name, ascSteps, descSteps} (basics.h:132-135) and engine/src/music.cpp:69-71 defines Harmonic Minor, Melodic Minor, Enigmatic with asc != desc, so the two walkers do produce different pitches on a downward step in those scales. Call paths confirmed: composer.h:549 realizes with step_note; piece_utils.h:37 pitch_before() and piece_utils.h:120 update_range_from_figure() walk with PitchReader::step, and pitch_before() seeds phrase.startingPitch in phrase_strategies.h:51/104, two_figure_phrase_strategy.h:32, wrapper_phrase_strategy.h:32, elaborated_phrase_strategy.h:41, composer.h:1656/1868/1974 — so realized pitches and the cursor the strategies see can disagree on those scales. Internal PitchReader inconsistency also confirmed: step_up adds asc[deg] then step_down subtracts desc[deg] for the same degree, leaving noteNumber != update_note_number()'s asc-only recomputation (pitch_reader.h:107-112), which set_octave/set_degree/snap_to_degree then snap to. Degree-search duplication confirmed verbatim (lines 47-55 vs 60-68), and the deg=0 fallback for an off-scale input is real (no match leaves deg at its initializer). Severity call: medium — concrete latent correctness divergence, but no score or template in the repo currently selects an asc!=desc scale (only a comment at templates.h:480 mentions Harmonic Minor), so nothing shipped is affected today.

## F108 [CONFIRMED] medium (reporter: medium) soundness — engine/include/mforce/music/random_figure_builder.h:52

**Function-local static Randomizer for figure count breaks seed reproducibility**

Reporters: arch-modern-cpp, music-builders-voicing

Evidence: `:52-53 `static Randomizer countRng(0x0C117u); return countRng.int_range(4, 8);` — reached whenever `!c.count && !(c.length && c.defaultPulse)`; elaborated_phrase_strategy.h:73 passes JSON `cfg.buildConstraints` through build_by_length so defaultPulse may be absent.`

Why: The count then depends on how many builds any other RandomFigureBuilder in the process has done before, not on this builder's seed — violates the project's non-negotiable that seeds in JSON reproduce output. It is also a process-wide mutable global in a header.

Recommendation: Draw the count from a member stream (e.g. stepGen.rng or a third seeded Randomizer constructed from `seed + 2`).

Also reported as: Function-local static Randomizer shared by all builders breaks per-seed reproducibility and is a data race across composers

- verifier: Verified: random_figure_builder.h:52-53 is exactly `static Randomizer countRng(0x0C117u); return countRng.int_range(4, 8);` inside inline resolve_count_, reached when !c.count && !(c.length && c.defaultPulse). Reachability confirmed: build_by_length(...) at two_figure_phrase_strategy.h:59 (cfg.constraints) and elaborated_phrase_strategy.h:73 (cfg.buildConstraints) pass JSON-loaded Constraints whose defaultPulse is optional (templates_json.h:86 only sets it if present); the elaborated :118 fallback covers only the generate path, not the ByLength skeleton path. Other callers (library_passage_strategy.h:152, passage_strategies.h:122) always set defaultPulse so they never hit it; no shipped score exercises the path (test_elaborated.json uses by_count). Consequence holds: a function-local static in an inline header function is one process-wide instance, so the count depends on how many prior builds hit this branch anywhere in the process, not on this builder's seed. Alt-title 'data race across composers' is NOT supported: no std::thread/std::async anywhere in engine/ or tools/. Severity medium: real violation of seed reproducibility on an edge path, trivial fix.
- verifier: Evidence confirmed at random_figure_builder.h:52-53: `static Randomizer countRng(0x0C117u); return countRng.int_range(4, 8);` inside an inline member in a header (one process-wide instance). Randomizer::int_range -> value() -> uni01(rng) advances the mt19937 (randomizer.h:43,73), so the count returned depends on how many prior builds in the process hit this path, not on the builder's seed. Reachability confirmed: elaborated_phrase_strategy.h:73 build_by_length(cfg.buildLength, cfg.buildConstraints) and two_figure_phrase_strategy.h:59 build_by_length(cfg.length, cfg.constraints) pass JSON-parsed Constraints; templates_json.h:86 only sets defaultPulse `if (j.contains("defaultPulse"))`, so an authored ByLength config without defaultPulse lands on the static. With c.length set, wander_ then sizes the figure to the static-drawn count (lines 80-89), so the output differs. The other callers (library_passage_strategy.h:152, passage_strategies.h:122) always set defaultPulse and are unaffected. Caveats: the alt-title's 'data race across composers' is NOT substantiated (grep finds no std::thread/async/omp in engine/ or tools/); and the only shipped elaborated score (scores/baselines/test_elaborated.json) uses by_count, so the break is latent today. Medium: violates the seeds-reproduce rule on a reachable path but no current artifact exercises it.

## F109 [CONFIRMED] high (reporter: high) soundness — engine/include/mforce/music/random_figure_builder.h:62

**build() never verifies count or length; zigzag/neighbor/leap_fill emit wrong unit counts and wander_ pads/truncates pulses so total length != c.length**

Reporters: music-builders-voicing

Evidence: `satisfies_ :63-70 checks only net/ceiling/floor. try_zigzag_ :176 `int cycles = (count - 1) / 2;` → zigzag emits 1+2*cycles = count-1 for even count. try_neighbor_ :187-188 `run(direction, count - 2, pulse); return figure_transforms::combine(base, tail, 0, false);` → combine concatenates (figure_transforms.h:148-150) = 3+(count-2) = count+1. try_leap_fill_ :194 `int fillSteps = count - 2;` → for count==2 passes 0, and shape_figures.h:51 `if (fillSteps <= 0) fillSteps = leapSize - 1;` → 4 units. wander_ :88-89 `while (ps.pulses.size() < ss.count()) ps.add(pulse); ps.pulses.resize(ss.count());``

Why: The class header (:17) promises "Satisfy every set constraint or throw". build_by_count(4) returns 3 units ~10% of the time (zigzag) and 5 units ~10% (neighbor); build_by_length(L) returns a figure whose duration sum differs from L whenever generate() returns a different pulse count than round(L/pulse), or when L is not a multiple of defaultPulse (shape helpers use count*pulse). Callers already compensate: elaborated_phrase_strategy.h:119-121 builds E_i by anchor.duration and then "Defensive: ensure it"; passage_strategies.h grid_complete exists to re-quantize passage length. Phrase/passage lengths drifting off the bar grid is directly audible.

Recommendation: Add count and length (within epsilon) checks to satisfies_. Remove the 0-means-default sentinels from shape_figures (leap_and_fill fillSteps, arc returnExtent) and make each try_* compute an exact count (neighbor: run(count-3) appended; zigzag: pad the trailing unit or make it ineligible for even counts). In wander_, generate the pulse sequence first and size the step sequence to ps.count() instead of resizing pulses to the step count.

- verifier: All evidence verified at cited lines. :17 comment 'Satisfy every set constraint or throw'; satisfies_ :62-71 checks only net/ceiling/floor, never count or total length. try_zigzag_ :176 cycles=(count-1)/2 -> zigzag emits 1+2*cycles = count-1 for even count (author comment at :175 even admits 'trailing unit may be off-grid'). try_neighbor_ :184-188: neighbor()=3 units + run(count-2) joined by combine(base,tail,0,false) (figure_transforms.h:139-150: elideCount 0 -> no prune, pure concatenation) = count+1 units. try_leap_fill_ :194-196: count==2 -> fillSteps 0 -> shape_figures.h:51 defaults to leapSize-1=2 -> 4 units; canLeapFill is true at count>=2 (:211). wander_ :75-77 draws pulseGen.generate(*c.length,pulse) (figures.h:394-455 returns a variable-count sequence summing to length), then :88-89 pads with `pulse`/resizes to count, changing the total. Shape helpers emit count*pulse so L not a multiple of pulse also misses L. Weights at :222-227 give zigzag 10% / neighbor 10% for count=4, matching the ~10% claims. Also unflagged bonus: try_arc_ with |net|==count-1 yields returnExtent=0 which shape_figures::arc(:68) re-defaults to extent, doubling the figure. Caller compensation: elaborated_phrase_strategy.h:119-121 'Defensive: ensure it' is actually about step[0]=0, not length (minor misattribution), but that strategy does splice E_i in place of an anchor with no length reconciliation, so phrase length drifts. grid_complete (passage_strategies.h:133-151) comment confirms '17 of 23 engine passage renders ended at things like 9.38 and 37.25' downstream of sample_cell->build_by_length, which is this mechanism. Severity high: documented contract broken, audible off-grid phrase/passage lengths.
- verifier: All cited evidence confirmed. satisfies_ (:62-71) checks only net/ceiling/floor; header :17 promises 'Satisfy every set constraint or throw'. try_zigzag_ :176 cycles=(count-1)/2 -> shape_figures::zigzag emits 1+2*cycles (shape_figures.h:87-91) = count-1 for even count (the comment at :175 even admits 'trailing unit may be off-grid'). try_neighbor_ :187-188 run(dir, count-2) emits count-2 units (shape_figures.h:16-18) and combine(..., 0, false) concatenates with elideCount=0 (figure_transforms.h:148-150, 158-160) -> 3+(count-2)=count+1. try_leap_fill_ :194-196 passes fillSteps=0 for count==2 and shape_figures.h:51 rewrites 0 to leapSize-1=2 -> 4 units. wander_ :88-89 pads/truncates ps to ss.count() so sum(duration)!=c.length whenever generate() returns a different pulse count. Shape helpers use count*pulse so build_by_length(L) with L not a multiple of defaultPulse (e.g. sample_cell beats=4, pulse=1.5 -> count 3 -> 4.5 beats) also misses L. Percentages check out for count=4 (eat weights 0.30/0.20/0.10/0.10/0.10 at :222-226: zigzag 10% -> 3 units, neighbor 10% -> 5 units). Consequence confirmed in-tree: passage_strategies.h:132-140 grid_complete comment reports 17 of 23 passage renders ended off-grid; composer.h:277 calls it. One inaccuracy: the 'Defensive: ensure it' at elaborated_phrase_strategy.h:120-121 is about units[0].step=0, not length; the actual length workaround there is :118 (defaultPulse = anchor.duration/4 so count*pulse matches). Bonus instance of the same sentinel class: try_arc_ with |net|==count-1 yields returnExtent=0, and shape_figures.h:68 turns 0 into full return (count=3, net=2 -> 5 units, net 0). Also no tests reference build_by_count/build_by_length (grep), so nothing guards the contract. High: documented contract violated on 20%+ of draws with an audible, already-measured downstream cost.

## F110 [CONFIRMED] low (reporter: low) smell — engine/include/mforce/music/random_figure_builder.h:94

**No-op placeholder functions kept as "seams": post_clamp_ and ChordWalker::melody_aware_start**

Reporters: music-builders-voicing

Evidence: `random_figure_builder.h:94-99 `post_clamp_(MelodicFigure fig, const Constraints& c) { // Round-1 no-op ... (void)c; return fig; }`; chord_walker.h:219-224 `melody_aware_start(...) { if (constraint.melodyProfile.empty()) return constraint.startChord; ... return constraint.startChord; }``

Why: Both branches return the same thing; the names promise behavior that does not exist and the extra MelodicFigure copy in post_clamp_ is pure cost.

Recommendation: Delete both; reintroduce when there is an implementation.

- verifier: Both locations verified. random_figure_builder.h:94-99 post_clamp_ takes MelodicFigure by value, does (void)c, returns fig unchanged; called at :229 as `fig = post_clamp_(fig, c)` so the lvalue argument is copied once per attempt (vector<FigureUnit> copy, not on a render path). chord_walker.h:219-224 melody_aware_start: both branches return constraint.startChord; called at chord_walker.h:55. Names promise behavior that does not exist. Copy cost is real but negligible (comp lane, offline). Severity low: smell/cleanup only.
- verifier: Both locations confirmed. random_figure_builder.h:94-99 post_clamp_ takes MelodicFigure by value, does `(void)c; return fig;` and is called as `fig = post_clamp_(fig, c)` at :229, which copy-constructs the parameter from the lvalue (one vector copy, then move back) — pure cost, tiny. chord_walker.h:219-224 melody_aware_start returns constraint.startChord on both branches; called once at chord_walker.h:55. Names promise behavior that does not exist; no other references (grep). Low: style/smell with negligible concrete cost.

## F111 [CONFIRMED] medium (reporter: medium) soundness — engine/include/mforce/music/random_figure_builder.h:148

**try_arc_ throws inside the retry loop for |net| > count-1 while feasible_ ignores net, so the same constraints succeed or throw depending on the seed**

Reporters: music-builders-voicing

Evidence: `:155 `if (absN > total) throw std::runtime_error("try_arc_: net too large for count");` — feasible_ :56-60 checks only count>=1 and minPulse*count; build :205-231 does not catch; wander_ :81 `stepGen.targeted_sequence(count - 1, *c.net)` always lands (figures.h:257-259 `step = distance`).`

Why: For net=4,count=3 the 30% arc branch escapes build() with a runtime_error on the first attempt it is drawn, while the wander_ branch returns a figure. passage_strategies.h:123-125 clamps net to count-1 specifically to dodge this ("|net| must fit inside the cell's own span or the shape helpers throw"). run/zigzag/leap_fill likewise ignore net magnitude and rely on 3 retries to get lucky.

Recommendation: Reject |net| > count-1 in feasible_ and make the canArc/canRun/canZigzag/canLeapFill eligibility net-aware (e.g. canRun only when !net || |net|==count-1), so build() picks from shapes that can satisfy the constraint instead of retrying blind.

- verifier: Verified: :155 `if (absN > total) throw std::runtime_error("try_arc_: net too large for count")` inside try_arc_ (:148-165). feasible_ :56-60 checks only count>=1 and minPulse*count<=length, nothing about net. build :205-231 has no try/catch, so the throw escapes build() on the attempt that draws arc (30% when count>=3, :212/:222). wander_ :81 targeted_sequence(count-1,*c.net) always lands: figures.h targeted_sequence forces `step = distance` on the last step. For net=4,count=3: run gives net 2, zigzag 1, neighbor 1, leap_fill 2 -> all fail satisfies_ and retry; only wander (20%) succeeds; arc (30%) throws -> outcome is seed-dependent as claimed. passage_strategies.h:123-125 verified verbatim: clamps net to count-1 with the quoted comment. Callers: library_passage_strategy.h:156-163 and the wandering strategy (passage_strategies.h:647+) catch; two_figure_phrase_strategy.h and elaborated_phrase_strategy.h do not, so the runtime_error propagates there. Severity medium: seed-dependent exception on a non-realtime path with partial caller workarounds.
- verifier: Confirmed: :155 throws runtime_error when |net| > count-1; feasible_ :56-60 checks only count>=1 and minPulse*count<=length; the retry loop :205-231 has no try/catch, so the throw escapes build() on the first attempt that draws arc (30% weight at :222, canArc for count>=3). wander_ :81 targeted_sequence(count-1, *c.net) always lands: figures.h:257-259 forces `step = distance` on the last step (traced net=4,count=3: steps [3,1], net 4, passes satisfies_). So identical constraints throw or succeed depending on the seed. passage_strategies.h:123-125 confirmed clamping net to count-1 with the comment 'or the shape helpers throw'. run/zigzag/leap_fill produce fixed nets (±(count-1), ±cycles, leapDir*(3-fillSteps)) and merely fail satisfies_, burning one of three retries, as claimed. Only library_passage_strategy.h:157-164 catches; elaborated_phrase_strategy.h:70-77 and two_figure_phrase_strategy.h:56-62 do not, so a JSON net larger than count-1 propagates. Medium: seed-dependent exception on a user-authorable constraint, feasible_ is the obvious place to reject it.

## F112 [CONFIRMED] medium (reporter: medium) smell — engine/include/mforce/music/random_figure_builder.h:199

**"Random" builder is deterministic given (count, pulse, net) for 70% of its shape mass; callers compensate by re-rolling inputs**

Reporters: music-builders-voicing

Evidence: `try_arc_ :148-165, try_zigzag_ :174-180, try_leap_fill_ :191-197 reference no RNG; :192 `int leapSize = 3; // round-1 default; tuning later`; :179 `zigzag(direction, cycles, 2, 1, pulse)`. Caller passage_strategies.h:110-116: "a bare build_by_length(3.0) with a fixed pulse returns the SAME cell for every seed: two pedal_buildup takes at seeds 7311 and 8311 rendered byte-identical note lists."`

Why: The seed only chooses which shape (one coin at :215) and feeds wander_; arc split, leap size, zigzag step/skip and neighbor direction (a coin, but only 10% weight) are fixed. Variety has to be manufactured upstream by drawing pulse/net per candidate, which pushes builder responsibility into every caller.

Recommendation: Have each try_* draw its free parameters from the builder's RNG within the constraint (leapSize ∈ [2,4], zigzag stepSize/skipSize, arc extent split when net is unset), so the seed drives variety inside the contract.

- verifier: Verified: try_arc_ (:148-165), try_zigzag_ (:174-180), try_leap_fill_ (:191-197) reference no RNG; :192 `int leapSize = 3; // round-1 default; tuning later` and :179 `zigzag(direction, cycles, 2, 1, pulse)` quoted exactly. try_run_ (:167-172) draws a coin only when c.net is unset; try_neighbor_ (:184) draws one coin; wander_ uses stepGen/pulseGen. With net given, arc 30 + run 20 + zigzag 10 + leap_fill 10 = 70% of shape mass is deterministic given (count, pulse, net), matching the claim; the only seed influence for those branches is the shape pick at :215 `float r = pulseGen.rng.value()`. passage_strategies.h:107-116 comment quoted accurately, including the 7311/8311 byte-identical measurement, and sample_cell (:117-130) draws pulse and net per candidate as the workaround. Nuance: the source comment's 'SAME cell for every seed' overstates slightly since :215 varies the shape by seed, but the finding's actual claim (helpers deterministic given inputs; variety pushed to callers) is correct. Severity medium: concrete documented cost (identical renders across seeds, per-caller workaround).
- verifier: Confirmed: try_arc_ (:148-165), try_zigzag_ (:174-180), try_leap_fill_ (:191-197, leapSize=3 hardcoded with 'tuning later') reference no RNG; try_run_ draws a coin only when net is unset (:170); try_neighbor_ draws one coin (:184); the shape choice is a single pulseGen.rng.value() at :215. With net set, arc+run+zigzag+leap_fill = 0.70 of the weight is fully determined by (count, pulse, net), matching the claim; with net unset run adds a coin so strictly 0.50 is fixed plus a 2-outcome run — the finding's framing 'given (count, pulse, net)' is the net-set case and is accurate. Caller quote confirmed verbatim at passage_strategies.h:110-116 (seeds 7311/8311 byte-identical), and the compensation (drawing pulse and net per candidate) is at :119-125; library_passage_strategy.h:141-152 likewise draws pulseHint per motif. Medium: documented concrete cost (duplicate takes across seeds) that has pushed workaround logic into two callers; not a correctness bug.

## F113 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/music/realization_strategy.h:79

**Composer<->Conductor chord boundary is still split across both tiers, contrary to the recorded decision**

Reporters: arch-music-model

Evidence: `realization_strategy.h:79-80 `// At Stage 9 this rewrites to emit per-tone Notes once Chord is dropped from Element variant.` and 85-89 emit a Chord Element; structure.h:90 `else if (e.is_chord()) endBeat += e.chord().dur;  // remove with stage 9`; conductor.h:316-328 ChordPerformer::perform_chord dispatches to `perform_with_figure` (418-471, Josie arpeggio/strum realization) or `perform_block` (405-416, per-voice spread timing); register_josie_figures (331-381) is live from mforce_cli:469 and mforce_ui:8126. docs/ComposerRefactor3.md:28-30: 'Let the Composer choose chord voicings *and* chord note realization ... Limit Performers to tempo alterations (swing, humanization), and articulation and ornament realization'.`

Why: Stage 9 never landed, so chord realization has two owners: Composer voices (VoicingSelector/VoicingPin) and emits a Chord event, then Conductor re-realizes it with strum spread and optional named figures. The spec's stated separation ('Conductor only consumes ElementSequence') is therefore only true for melody; a score export of chord parts cannot show what will sound, and ChordPerformer carries ~200 lines of figure logic the doc says belongs to the Composer.

Recommendation: Either finish Stage 9 (Composer emits per-tone Notes with explicit onsets/spread; drop Chord from Element; delete ChordPerformer's figure path) or update ComposerRefactor3.md to state that block-chord articulation is deliberately a Performer concern and remove the 'remove with stage 9' markers.


## F114 [REFUTED] low (reporter: medium) duplication — engine/include/mforce/music/rhythm_util.h:54

**Weighted roulette selection hand-rolled in eight places; rhythm generator duplicated against PulseGenerator**

Reporters: arch-duplication

Evidence: `roulette loops at rhythm_util.h:54-61, chord_walker.h:159-169 and 310-315, randomizer.h:91-99, pool_figure_builder.h:75-80, figures.h:443-449 and 483-489, figure_transforms.h:477-482, random_figure_builder.h:215-227; rhythm_util.h generate_musical_rhythm 25-68 duplicates figures.h PulseGenerator::generate 394-462 (binary path) with the BINARY table at 399 vs 473; degree lookup pitch_walker.h:46-57 vs 59-71; style_table.h:160-171 vs 173-184; chord.cpp:223-234 vs 236-248; MelodicFigure ctor figures.h:579-587 vs ChordFigure 613-621; humanize jitter conductor.h:28, 297, 412, 454.`

Why: Randomizer already has the weighted pick (91-99); the other seven copies each consume the RNG differently, which matters here because seeds are stored for reproducibility — consolidating later will change outputs, so the longer this waits the more baselines move.

Recommendation: `Randomizer::weighted_index(std::span<const float>)` as the single home; replace the copies in one change and re-pin the golden hashes once.

- verifier: Evidence check: every cited location exists and is materially as described — roulette loops at rhythm_util.h:54-60, chord_walker.h:159-169 and 310-315, core/randomizer.h:91-99 (select_int weighted), pool_figure_builder.h:75-80 (int_range variant), figures.h:443-449 and 483-489, figure_transforms.h:477-482 (subtractive variant), random_figure_builder.h:215-227 (eat lambda); generate_musical_rhythm (rhythm_util.h:25-68) is a near-verbatim copy of PulseGenerator::generate's binary path with the same table at figures.h:399/473; pitch_walker.h:46-57 vs 59-71, style_table.h:160-171 vs 173-184, chord.cpp:223-234 vs 236-248 (engine/src/chord.cpp, not src/music/), MelodicFigure vs ChordFigure ctors, and conductor.h inlining humanize*0.001f*rng.valuePN() at 297/412/454 despite jitter() existing at 28 — all confirmed. Consequence check fails: the 'why' claims the copies 'each consume the RNG differently' so consolidating will change outputs. Not so — every roulette copy draws exactly one rng.value() (int_range at randomizer.h:73-75 is also a single value() draw), so a shared weighted_index would reproduce the same index for the same draw; the only divergence is tie semantics (accum >= pick vs roll <= accum vs draw -= w; draw <= 0), which differ only at exact float boundaries. The 'baselines move the longer this waits' urgency is therefore unsupported. Further weakening the anchored finding: generate_musical_rhythm is never called anywhere in engine/ or tools/ (only its definition matches; rhythm_util.h is included by shape_strategies.h solely for direction_sign, used at composer.h:953) — the primary cited duplicate is dead code, so its runtime cost is zero and the right fix is deletion, not consolidation. No golden-hash harness exists to 're-pin' (only a 'golden render' comment in default_strategies.h). The non-roulette items (chord.cpp, pitch_walker, style_table, ctors) are unrelated padding under a roulette title. Net: real but style-level duplication plus one dead function; no concrete cost established; low.

## F115 [CONFIRMED] low (reporter: medium) soundness — engine/include/mforce/music/rng.h:11

**Ambient thread_local RNG dereferenced without a null check; public strategy dispatchers can run without a Scope installed**

Reporters: arch-modern-cpp

Evidence: `8: `inline thread_local Randomizer* current = nullptr;`  11: `inline uint32_t next() { return detail::current->rng(); }`; composer.h public dispatchers at 241-254 invoke strategies directly without installing rng::Scope`

Why: Any strategy that calls mforce::rng::next() outside a Scope null-dereferences; hidden thread-local state also makes seed reproducibility (a project non-negotiable) depend on call-order rather than data.

Recommendation: Assert/throw on null in next(), and pass the Randomizer explicitly through Locus or the strategy interface instead of an ambient global.

- verifier: Evidence verified: rng.h:8 `inline thread_local Randomizer* current = nullptr;` and rng.h:11 `next()` dereferences it unchecked; composer.h:241-275 public dispatchers (compose_figure/compose_phrase/compose_passage) do not install a Scope. BUT the consequence is latent only: every in-tree entry into those dispatchers goes through Composer::compose() -> compose_passage_ (Scope at composer.h:884) or generate_default_passage_ (Scope at composer.h:819); tools/ (mforce_cli, engine_tests, test_figures) and classical_composer.h only call composer.compose(), and no file outside engine/ calls rng::next or the dispatchers directly. All 9 rng::next sites are inside strategies reached only under a Scope. The 'reproducibility depends on call-order rather than data' part does not follow: seeds are data (ft.seed/masterSeed) and the fallback draw order is identical to the pre-refactor direct use of rng_ (commit 8fd87d6 deliberately kept that, see rng.h:14-17 comment); thread_local adds no new order dependency on the single-threaded compose path. Net: unenforced precondition on a public API, no reachable crash today; an assert is cheap. Low, not medium.

## F116 [CONFIRMED] low (reporter: medium) smell — engine/include/mforce/music/rng.h:11

**rng::next() is a thread_local service locator that dereferences a possibly-null pointer with no check**

Reporters: music-model

Evidence: `inline thread_local Randomizer* current = nullptr;
}
inline uint32_t next() { return detail::current->rng(); }`

Why: Strategies and the composer call ::mforce::rng::next() as a fallback seed source (composer.h:963, 1126, 1165, 1199, 1205, 1503, 1611, 1703; period_passage_strategy.h:146). Any of them invoked outside a Composer::compose() Scope (tools, tests, a future UI-driven compose) segfaults with no message. It is also hidden global state in a codebase that otherwise passes Randomizer& explicitly (figure_transforms, RandomFigureBuilder), so call order across threads or nested composes is invisible at the call site.

Recommendation: At minimum assert/throw when `current` is null. Better: since Scope already holds Composer's rng_ by reference, pass that Randomizer& (or a seed) through Locus and drop the thread_local.

- verifier: Quoted evidence is exactly rng.h:8-11. All cited call sites verified (composer.h:963, 1126, 1165, 1199, 1205, 1503, 1611, 1703; period_passage_strategy.h:146). The 'segfaults when invoked outside a Composer::compose() Scope' scenario is hypothetical: grep across engine/ and tools/ finds no tool, test, or UI code calling strategies or rng::next outside compose(); both compose paths install rng::Scope (composer.h:819, 884). Minor inaccuracy: RandomFigureBuilder takes a seed (composer.h:1205 `rfb(::mforce::rng::next())`), not Randomizer&, so only figure_transforms is a true explicit-Randomizer& counterexample. The thread_local design is a deliberate refactor (commit 8fd87d6 'RNG as thread-local singleton') and the ambient-vs-explicit choice is a style preference with no demonstrated cost. Null-check/assert part is valid but low severity; the drop-the-thread_local recommendation is a design preference.

## F117 [CONFIRMED] medium (reporter: medium) soundness — engine/include/mforce/music/smooth_voicing_selector.h:32

**No-scale "fallback" null-dereferences: Scale{} has uninitialized raw pointers and resolve() calls scale.length() immediately**

Reporters: music-builders-voicing

Evidence: `:32-33 `if (!req.scale) { return req.scaleChord.resolve(Scale{}, ...); }` — basics.h:151-155 `struct Scale { const PitchDef* pitchDef; const ScaleDef* scaleDef; int length() const { return scaleDef->length(); }` — chord.cpp:256 `int scaleLen = scale.length();``

Why: The branch presents as a graceful degradation but is a guaranteed crash; the composer always passes &activeScale so it is currently unreachable, which means any future caller that trips it gets a segfault instead of a diagnostic.

Recommendation: Throw std::invalid_argument ("VoicingRequest.scale is required") or assert; do not construct a Scale{} with null defs.

- verifier: Verified smooth_voicing_selector.h:32-34 returns req.scaleChord.resolve(Scale{}, ...). basics.h:151-156 Scale is an aggregate of two raw pointers with length() derefing scaleDef; chord.cpp:256 (ScaleChord::resolve) calls scale.length() first thing. Minor wording error in the finding: Scale{} value-initializes the pointers to nullptr, not 'uninitialized' — but the result is the same guaranteed null deref. Only caller is composer.h:721 which always passes &activeScale (stack local, never null), so the branch is unreachable today; the consequence (future caller gets a segfault, not a diagnostic) follows. No other VoicingRequest construction exists in engine/ or tools/ (grep).

## F118 [CONFIRMED] medium (reporter: medium) smell — engine/include/mforce/music/smooth_voicing_selector.h:41

**Dictionary lookup uses catch(...) as control flow and silently falls back to canonic voicings on a typo**

Reporters: music-builders-voicing

Evidence: `:41-48 `try { const auto& dict = ChordDictionary::get(req.dictionaryName); sc.quality = &dict.get_chord_def(...); } catch (...) { // leave sc.quality as-is }` — chord.cpp:150-151 and :61-62 throw on unknown dictionary / unknown chord.`

Why: A misspelled `voicingDictionary` in a template (or a chord quality the dictionary lacks) produces no diagnostic and quietly voices with the canonic set; catch(...) also swallows bad_alloc and anything else. The lookup is repeated per chord although the dictionary is fixed per passage.

Recommendation: Resolve the ChordDictionary once at template load (fail loudly on unknown), carry `const ChordDictionary*` in VoicingRequest, and only tolerate a missing chord-def with an explicit warning.

- verifier: Verified :40-48 try/catch(...) around ChordDictionary::get + get_chord_def. chord.cpp:147-152 ChordDictionary::get throws runtime_error on unknown name; :59-63 get_chord_def throws on unknown chord. dictionaryName comes straight from template JSON (templates_json.h:1078 voicingDictionary, composer.h:724), so a typo silently voices with the canonic ChordDef with no stderr line (contrast composer.h:636-640 which does warn on an unknown voicingSelector). catch(...) does swallow everything. Lookup is per select() call, i.e. per chord — true, though it is an unordered_map find at compose time, not a render-path cost, so the repetition is a nit; the silent-fallback-on-typo part is the real cost. Note the comment at :37-38 documents the fallback as intended, which does not change the lack of diagnostic.

## F119 [CONFIRMED] medium (reporter: medium) soundness — engine/include/mforce/music/smooth_voicing_selector.h:53

**First chord of every Part ignores allowedInversions/allowedSpreads/cadential (returns inv=0, spread=0 whenever previous is null)**

Reporters: music-builders-voicing

Evidence: `:51-53 `Chord base = sc.resolve(*req.scale, req.rootOctave, req.durationBeats, 0, 0); if (!req.previous || base.pitches.empty()) return base;` — composer.h:673 `const Chord* prevChord = nullptr;` is passed as req.previous (:722) for chordIdx 0.`

Why: An authored profile such as allowedInversions:[1] or a scripted sequence whose first entry pins spread is silently violated on chord 0 of each passage; the allow-list filtering (:79-92) runs only after the early return.

Recommendation: When previous is null, still enumerate the allow-listed (inv, spread) candidates and return the first allowed one (or score by register alone), so the profile is honored from the first chord.

- verifier: Verified :51-53: base = sc.resolve(scale, rootOctave, dur, 0, 0) and early return when !req.previous, before the allow-list lambdas (:79-89) and the cadential restriction (:80) are ever consulted. composer.h:673 sets prevChord=nullptr inside the per-section/per-part scope and emit_chord (:721-722) passes it as req.previous, so chord 0 of each passage (unless it carries a VoicingPin, :688) always gets inv=0 spread=0 regardless of profile.allowedInversions/allowedSpreads/cadential. Consequence follows; 'first chord of every Part' is more precisely 'first chord of each section-passage on each Part' since prevChord is reset per section.

## F120 [CONFIRMED] low (reporter: low) soundness — engine/include/mforce/music/smooth_voicing_selector.h:208

**melody_penalty is unreachable (melodyPitch never set) and its magnitudes are not normalized against the [0,1] composite**

Reporters: music-builders-voicing

Evidence: `:102-104 `cand.melody = req.melodyPitch ? melody_penalty(...) : 0.0f;` — composer.h:722 passes `std::nullopt`; :218-220 `penalty += 4.0f; ... penalty += 2.0f; else penalty -= 2.0f;` added at :150 to vlNorm/ctNorm terms that are min-max normalized to [0,1].`

Why: Dead today; if the melody hint is ever wired in, a ±2..6 term swamps the normalized VL/CT blend and priority stops mattering. The header comment (:24) calls it "stubbed" but it contains tuned constants.

Recommendation: Remove until melody-aware voicing is designed, or express the melody term on the same [0,1] scale with an explicit weight.

- verifier: Verified :102-104 gates melody_penalty on req.melodyPitch; the sole VoicingRequest construction in the codebase (composer.h:721-724) passes std::nullopt, and grep across engine/ and tools/ finds no other construction or assignment to melodyPitch, so melody_penalty (:208-222) is dead. Magnitudes confirmed: +4 / +2 / -2 added raw at :150 to vlNorm and ctNorm which are min-max normalized to [0,1] (:140-144) plus a 0.01 tiebreak — a ±2..6 term would dominate the (1-p)*vl + p*ct blend if wired in. Header comment :24 says 'stubbed'. Claim and consequence hold; the original 2026-04-20 plan specified these constants as intentional hard-ish penalties, so this is a design-scale question rather than a bug today.

## F121 [UNVERIFIED] high (reporter: high) duplication — engine/include/mforce/music/structure.h:215

**Harmony context is stored twice per Section and the two copies are read by different consumers with different scale resolution**

Reporters: arch-music-model

Evidence: `structure.h:215-216 `std::optional<ChordProgression> chordProgression; HarmonyTimeline harmonyTimeline;` both written from one source (composer.h:345-349 and 379-382; 879-881 in the walker path). Readers split: melody ChordFigure realization reads the progression and the SECTION scale -- composer.h:524 `if (isChordFig && section.chordProgression)` ... 538 `auto resolved = prog.chords.get(chordIdx).resolve(section.scale, kBaseOctave);` -- while harmony parts read the timeline with the ACTIVE scale and honour passage-local progressions -- 614-619 `const HarmonyTimeline* timeline = &sec.harmonyTimeline; if (passTmpl && passTmpl->chordProgression) {...}` and 686 `Scale activeScale = sec.active_scale_at(beatInPassage);`. AFS/PhraseAware also read chordProgression (1851, 1927); the anchor selector reads harmonyTimeline (1537).`

Why: Of the four harmony levels, the chord level has two representations that can disagree: a passage-local chordProgression (templates.h:518) or a KeyContext changes what the accompaniment voices but not what a chord-tone melody walks, so melody and accompaniment can be on different chords and in different keys in the same bar (the comment at 609-612 documents the split as accepted). The duplicated state also means every writer must remember both fields (the walker path at 879-881 does; set_segment callers in period_passage_strategy.h do not update chordProgression). As a side cost, 538 heap-allocates a resolved Chord per NOTE and scans the progression linearly per note.

Recommendation: Make HarmonyTimeline the single source of truth: delete Section::chordProgression, give realize_phrase_to_events_ the same timeline the harmony part uses (passage-local override included), resolve with the active scale, and resolve once per chord span rather than per note. AFS/PhraseAware should take the timeline from Locus.harmony (already populated at 861).


## F122 [DISPUTED] low (reporter: low) smell — engine/include/mforce/music/style_table.h:15

**`using json = nlohmann::json;` at namespace-mforce scope in a header**

Reporters: music-builders-voicing

Evidence: `:15 `using json = nlohmann::json;` inside `namespace mforce {` — also music_json.h:13 and templates_json.h:9.`

Why: Every TU including any of these headers gets `mforce::json`; any local type or variable named json in namespace mforce becomes ambiguous, and the alias is only used inside this file.

Recommendation: Use `nlohmann::json` spelled out in the three signatures, or move the alias into the struct.

- verifier: Evidence location accurate: style_table.h:15, music_json.h:13, templates_json.h:9 all declare `using json = nlohmann::json;` inside `namespace mforce`. Consequence does not follow. (1) 'becomes ambiguous' is wrong C++: a local variable named json shadows the alias (no ambiguity); three identical alias-declarations in one namespace are a legal redeclaration; grep finds no competing `json` type/variable in namespace mforce; patch_loader.cpp:42 declares its alias at global scope, so inner-scope lookup inside namespace mforce wins with no conflict. (2) 'alias is only used inside this file' is false: tools/engine_tests/main.cpp:1273 and :1487 write `json tj = json::parse(...)` under `using namespace mforce;` with no alias of their own (the only `using json` lines in that TU are absent; nlohmann is spelled out only at :784), so the header-scope alias is load-bearing for consumers. Style preference with no concrete cost.
- verifier: Evidence confirmed: style_table.h:15 `using json = nlohmann::json;` sits directly inside `namespace mforce {`, and the same alias exists at music_json.h:13 and templates_json.h:9 (plus patch_loader.cpp:42, a .cpp, which is fine). The alias is used only at style_table.h:145 and :154; the sole other includer, chord_walker.h, uses `json` zero times. Consequence is overstated: a local VARIABLE named json merely shadows (no ambiguity); only a TYPE named `json` declared in namespace mforce would collide, and none exists. Identical repeated alias-declarations are legal C++, so the three headers do not conflict with each other. This is the pre-existing convention in two sibling headers (though melody_profile.h in the same directory spells nlohmann::json out, so the directory is already mixed). Style nit with no concrete cost.

## F123 [CONFIRMED] medium (reporter: high) soundness — engine/include/mforce/music/style_table.h:89

**ChordLabel round-trip is keyed on ChordDef::name strings that do not exist: V7 relabels as "V", viio as "VIIo" (unreachable row → silent I), parse("M7"/"m7") throws**

Reporters: music-builders-voicing

Evidence: `style_table.h:97 `sc.quality->name == "Minor" || ... "Minor7"`, :103 `if (qn == "7") result += "7";` vs chord.cpp:20-22 names are "Dominant 7th", "Major 7th", "Minor 7th"; style_table.h:74 `qualityName = "Major7"` → chord.cpp:46-51 ChordDef::get matches shortName or name only → throws "Unknown ChordDef: Major7"; chord_walker.h:267-268 `if (!transitions || transitions->empty()) return ScaleChord{0, 0, &ChordDef::get("Major")};``

Why: On the only shipped style table: after picking V7 the walker labels it "V" and consults the "V" row (the "V7" row and "ii,V7"/"IV,V7" overrides are unreachable as sources); after picking viio it labels it "VIIo", finds no row, and silently jumps to I Major. can_reach_target (chord_walker.h:293) is fed the same wrong labels. note_map.h:92-97 already acknowledges this ("it labels G7 as 'V'. Left untouched there (chord_walker depends on it); fixed here") and forks a corrected copy — a known-broken function kept alive plus a duplicate.

Recommendation: Key both parse() and to_string() on ChordDef::shortName ("", "m", "7", "M7", "m7", "dim", "+"): parse should pass the suffix straight to ChordDef::get (which already accepts shortName and the "M" alias) instead of translating it; to_string should mirror note_map.h:98-114. Then delete tendency_chord_label in note_map.h. Make the missing-row path in pick_next throw or log instead of silently returning I.

- verifier: All cited lines confirmed: style_table.h:97 compares quality->name to "Minor"/"Minor7", :103-105 to "7"/"Major7"/"Minor7"; chord.cpp:20-22 names are "Dominant 7th"/"Major 7th"/"Minor 7th" with shortNames "7"/"M7"/"m7"; chord.cpp:46-51 ChordDef::get matches shortName or name only, so parse("M7")->get("Major7") and parse("m7")->get("Minor7") throw; chord_walker.h:267-268 silently returns I Major on a missing row; :293 feeds to_string(t.target) to can_reach_target (:320-331, keyed on style.transitions by label). Traced: parse("V7") -> ChordDef "7" (name "Dominant 7th") -> to_string gives "V"; parse("viio") -> ChordDef "dim" (name "Diminished", isMinor false) -> to_string gives "VIIo" -> no row -> I. Walk is live: composer.h:874-878 loads StyleTable by sd->styleName and calls ChordWalker::walk; period_passage_strategy.h:342 loads it for harmonize. note_map.h:92-114 comment and corrected fork confirmed verbatim. Corrections to the finding: three tables ship (classical_mozart, classical_tonic_dominant, nursery_v1), not one; the V7 mislabel has near-zero practical effect on the shipped tables because V and V7 rows share targets (weights 6 vs 7, 7 vs 8); the viio->I collapse is real on classical_mozart (authored 5:2 I/vi becomes always I); the M7/m7 throw is latent (no shipped table uses those labels). Severity high for the silent authored-row misrouting plus the kept-alive known-broken function.
- verifier: Fully confirmed from code. basics.h:320-324 gives ChordDef field order shortName/displayName/name, so chord.cpp:20-22 entries have name = "Dominant 7th", "Major 7th", "Minor 7th" and shortName = "7", "M7", "m7". style_table.h:97 compares name against "Minor7" (nonexistent) and :103-105 against "7"/"Major7"/"Minor7" (nonexistent), so to_string(V Dom7) = "V" and any m7/M7 chord loses its suffix. parse: :74-75 translate "M7"->"Major7", "m7"->"Minor7", and ChordDef::get (chord.cpp:46-52) matches shortName or name only -> throws "Unknown ChordDef: Major7". viio: parse works (name "Diminished" matches) but to_string gives "VIIo" (uppercase, :97 isMinor false) which has no row in classical_mozart.json (key is "viio") -> lookup nullptr -> chord_walker.h:267-268 returns I Major silently. In walk(), chord_walker.h:81 relabels via to_string so the "V7" row (and tonic_dominant's "V7" row) is never consulted as a source; can_reach_target at :293 gets the same mislabel. note_map.h:92-114 comment and tendency_chord_label fork confirmed verbatim; STATUS.md:15-16 already logs the G7->"V" defect as known. Minor inaccuracy: "the only shipped style table" — three exist (classical_mozart, classical_tonic_dominant, nursery_v1); the first two both carry V7 rows. Severity downgraded from high to medium because the path is dormant: no score JSON in the repo sets "styleName" (composer.h:873 and period_passage_strategy.h:340 gate on it), so neither ChordWalker::walk nor harmonize runs for any shipped artifact; backlog 25 (melody-first harmonization) is DEFERRED. Becomes high the moment that path is exercised.

## F124 [CONFIRMED] medium (reporter: high) soundness — engine/include/mforce/music/style_table.h:130

**StyleTable overrides can never fire: walk() pushes the current label before lookup, so the back-off key is always "X,X"**

Reporters: music-builders-voicing

Evidence: `style_table.h:133 `std::string key = history.back() + "," + currentLabel;` — chord_walker.h:75 `history.push_back(currentLabel);` then :79 `current = pick_next(style, currentLabel, history, ...)` → :265 `style.lookup(currentLabel, history)``

Why: The override table is the only context-aware mechanism in the harmony walker ("chord transition graph with variable-order back-off"). Because history.back() is always the label just pushed (== currentLabel), the composed key is "I,I", "vi,vi", never the authored "I,vi" / "ii,V" keys in styles/classical_mozart.json:16-21. Every override row in the shipped table is dead; harmonize() passes `{}` for history (chord_walker.h:146) so overrides are dead there too. No .cpp test exercises ChordWalker::walk or StyleTable::lookup (grep found none), which is why this survived.

Recommendation: Make lookup take (prevLabel, currentLabel) explicitly, or push to history AFTER pick_next; drop the std::vector<std::string> history entirely since only the previous label is ever read. Add a test on classical_mozart.json asserting that after I→vi the "I,vi" row is consulted.

- verifier: Confirmed exactly: chord_walker.h:75 `history.push_back(currentLabel);` then :79 pick_next(style, currentLabel, history, ...) with no reassignment of currentLabel between; :265 `style.lookup(currentLabel, history)`; style_table.h:133 `key = history.back() + "," + currentLabel`. history.back() == currentLabel on every iteration, so the composed key is always "X,X". styles/classical_mozart.json:16-21 override keys are I,vi / V,vi / IV,V / IV,V7 / ii,V / ii,V7 (none self-paired) -> all dead. harmonize passes `{}` at :146 -> overrides dead there too. The other two shipped tables have no overrides. No test exercises walk or lookup: tools/engine_tests/main.cpp only touches StyleTable at :1665 (load_by_name + transitions.count); grep for ChordWalker/lookup( in engine_tests returns nothing. Walk is reachable from composer.h:878. Feature silently disabled = concrete cost.
- verifier: Confirmed. chord_walker.h:75 `history.push_back(currentLabel)` precedes :79 `pick_next(style, currentLabel, history, ...)` -> :265 `style.lookup(currentLabel, history)` -> style_table.h:133 `key = history.back() + "," + currentLabel`. history.back() is the value just pushed == currentLabel, so the key is always "X,X". classical_mozart.json:16-21 override keys are "I,vi", "V,vi", "IV,V", "IV,V7", "ii,V", "ii,V7" — none self-pairs — so every override row is unreachable. harmonize() at :146 passes `{}` so overrides are dead there as well. lookup reads only history.back(), confirming the vector is over-provisioned. StyleTable::lookup has exactly two callers (:146, :265), both broken for overrides. Test claim confirmed: grep of tools/ finds no ChordWalker reference; engine_tests/main.cpp:1665 only parses nursery_v1 via load_by_name. Severity medium rather than high for the same reason as F123: no score JSON sets styleName, so the walker/harmonizer is not on any live render path today (backlog 25 deferred). Real correctness bug with concrete cost once that path runs.

## F125 [CONFIRMED] low (reporter: low) duplication — engine/include/mforce/music/style_table.h:160

**parse_json repeats the transition-row parsing loop verbatim for transitions and overrides**

Reporters: music-builders-voicing

Evidence: `:161-170 and :174-183 are identical apart from the destination map: `for (auto& entry : arr) { Transition t; t.target = ChordLabel::parse(entry[0].get<std::string>()); t.weight = entry[1].get<float>(); ts.push_back(t); }``

Why: Small, but it is the exact place the label-parsing fix (above) must land, and two copies invite one being missed.

Recommendation: Factor `static std::vector<Transition> parse_rows(const json& arr)` and call it for both maps.

- verifier: Confirmed: style_table.h:161-170 and :174-183 are the same 8-line loop (Transition t; t.target = ChordLabel::parse(entry[0].get<std::string>()); t.weight = entry[1].get<float>(); ts.push_back(t)) differing only in the destination map (transitions vs overrides) and the loop variable name (label vs key). Nit-level duplication; the only cost is the one the finding names (a future parse fix landing in one copy).
- verifier: Confirmed. style_table.h:161-170 and :174-183 are the same seven-line loop body (Transition t; t.target = ChordLabel::parse(entry[0]...); t.weight = entry[1].get<float>(); ts.push_back(t)) differing only in the iteration variable name (label vs key) and destination map (transitions vs overrides). Legitimate small duplication; cost is only the risk of a one-sided edit when the F123 parse fix lands.

## F126 [REFUTED] low (reporter: high) efficiency — engine/include/mforce/music/templates.h:9

**nlohmann/json is a transitive dependency of the core data model: 38 of 128 engine headers and every tool TU compile 25,526 lines of json.hpp**

Reporters: arch-build-headers

Evidence: `templates.h:9 `#include <nlohmann/json.hpp>` ... :545 `nlohmann::json voicingProfileSelectorConfig;`; core/source_registry.h:3 `#include <nlohmann/json.hpp>` and :14-15 `using ResolveParamFn = std::function<std::shared_ptr<ValueSource>(const nlohmann::json& val)>;`; music_json.h:13 and templates_json.h:9 `using json = nlohmann::json;` at namespace mforce scope`

Why: Measured closure (scratchpad include-graph script): music/templates.h is in the closure of 22 headers, and its json include propagates so that 38/128 headers carry json.hpp; composer.h's closure is 51 headers / 11,883 engine lines + json; the mforce_cli main.cpp TU reaches 73 headers / 17,296 engine lines + 25,526 json lines, engine_tests 79 headers, test_figures 70. mforce_cli already needs `/bigobj` (tools/mforce_cli/CMakeLists.txt:5), which is the symptom of these closures. The `using json` alias injected into namespace mforce by two headers also leaks a short generic name into every includer. Any JUCE/plugin consumer that links mforce_engine inherits a json compile on every TU that touches a template.

Recommendation: (1) core/source_registry.h: include <nlohmann/json_fwd.hpp> only (std::function signatures need only the forward declaration). (2) templates.h: replace the `nlohmann::json voicingProfileSelectorConfig` member with an opaque value (e.g. `std::string rawJson` parsed by the selector at configure time, or a small `VoicingProfileSelectorConfig` struct), removing the json include from the data model. (3) Move templates_json.h and music_json.h bodies into engine/src/music_json.cpp behind a thin API (`PieceTemplate load_piece_template(const std::string&)`, `std::string to_json_text(const Piece&)`), and drop the namespace-scope `using json` aliases. Expected win: json leaves the closure of ~35 headers; mforce_cli/engine_tests/test_figures TUs shrink from ~43k preprocessed engine+json lines to ~17k; /bigobj can go.

- verifier: Evidence lines verified (templates.h:9 and :545; source_registry.h:3,14-15; music_json.h:13 and templates_json.h:9 'using json' — style_table.h:15 also has it, so three headers not two). Re-measured the closure independently: 128 headers, 38 json carriers, templates.h in the closure of 22 headers, composer.h 51, mforce_cli main.cpp 73, engine_tests 79, test_figures 70, json.hpp 25,526 lines — all numbers match. The stated consequences do NOT follow: (a) 'every tool TU' is false — 11 of 19 tool TUs (wav_check, durn_converter, stamp_test, 8 stk_ref) compile no json, and only 3 TUs (cli main, engine_tests, test_figures) reach json via templates.h; (b) all three of those TUs use json directly (cli main.cpp includes music_json.h/templates_json.h and calls json::parse at 529/689, builds nlohmann::json at 1030; engine_tests/main.cpp:773 and test_figures/main.cpp:24 include <nlohmann/json.hpp> themselves), so the promised '43k -> 17k preprocessed lines' shrink cannot happen; (c) /bigobj was added in 02680cf (2026-05-23) when explore.cpp was added to mforce_cli, and engine_tests/test_figures with LARGER closures build without it, so 'symptom of these closures' is unsupported; (d) the expected win of '~35 headers' is wrong — applying all three recommendations frees 17 of 38 headers; json stays in 21 (composer.h, default_strategies.h, passage_strategies.h, period_passage_strategy.h, phrase_strategies.h, etc.) via melody_profile.h, pool_figure_builder.h, style_table.h, voicing_profile_selector.h, pattern_library.h; (e) source_registry.h is in the closure of zero other headers (only cli/ui mains include it, both already json TUs) and json_fwd.hpp is not vendored (third_party/nlohmann/ has json.hpp only), so recommendation (1) frees nothing; (f) 0 of 13 engine/src .cpp TUs include templates.h/composer.h, so the engine library build is unaffected. The residual true observation — the data model carries a json member at :545 — has no demonstrated cost today; per CLAUDE.md header layout without a concrete cost is low.
- verifier: Evidence verified: templates.h:9 json include, :545 nlohmann::json member, source_registry.h:3/14-15, music_json.h:13 and templates_json.h:9 namespace-scope `using json`, /bigobj at tools/mforce_cli/CMakeLists.txt:5. Reproduced the closure independently (scratchpad script): 128 engine headers, 38 with json.hpp in closure, templates.h in 22 other headers' closures, composer.h 50 headers/11,934 lines, mforce_cli main.cpp 73 headers/17,369 lines, engine_tests 79, test_figures 70, json.hpp 25,527 lines. The consequences do not follow: (a) 'every tool TU' is false, 5 of 17 tool TUs reach json (durn_converter, stamp_test, wav_check, 8 stk_ref TUs do not); (b) only 3 of 13 engine src TUs compile json (patch_loader, source_registrations, source_registry) and all three are JSON loaders that need it; (c) the three tool TUs named as the symptom each include JSON headers directly and use nlohmann::json themselves (mforce_cli main.cpp includes music_json.h/templates_json.h/patch_loader.h and has 11 nlohmann::json uses; engine_tests main.cpp `#include <nlohmann/json.hpp>` at :773; test_figures main.cpp at :24), so the 'shrink from ~43k to ~17k' win is zero for them unless their own JSON I/O is also refactored; (d) '/bigobj is the symptom of these closures' is contradicted: engine_tests has a larger closure (79 headers) than mforce_cli (73) and does not need /bigobj, so the flag is driven by mforce_cli's own TU content; (e) recommendation (1) needs json_fwd.hpp, which is not vendored (engine/third_party/nlohmann/ holds only json.hpp). Residual legitimate point: the nlohmann::json member at :545 couples the core data model to json so a future JUCE/plugin TU touching PassageTemplate pays the include, and the namespace-scope `using json` alias is a real nit. No compile-time measurement given; header-heavy layout without demonstrated concrete cost is low per the review rules.

## F127 [CONFIRMED] low (reporter: medium) god-class — engine/include/mforce/music/templates.h:110

**FigureTemplate is a union-of-modes struct; PhraseTemplate/PassageTemplate pair a strategy string with 4 and 5 'only one populated' optional configs — hand-rolled tagged unions**

Reporters: music-json

Evidence: `FigureSource source{FigureSource::Generate};
// --- For Generate: constraints --- ... // --- For Reference / Transform --- ... // --- For shape-based generation --- ... // --- For Locked --- ... // --- For Literal ---
int shapeDirection{1};         // +1 = ascending, -1 = descending   (129)
FigureDirection direction{FigureDirection::Ascending};               (132)
std::string rhythmTransform;   // "retrograde", "stretch", "compress"   (155)
int figureCadenceType{0};      // 0=none, 1=half, 2=full               (168)
...
// Only one is populated at a time (matching the selected strategy).  (327-332)`

Why: ~30 fields of which only a `source`-dependent subset is meaningful; three overlapping direction/shape notions (shapeDirection int, direction enum, shape enum) that different consumers read (composer.h:1057 reads shapeDirection); stringly-typed rhythmTransform/contourTransform next to the TransformOp enum they duplicate; cadence type as magic int in two structs (168, 302). The serializer pays for it directly: to_json(FigureTemplate) 314-376 must guard field groups by source and still emits shape/motif-atom fields regardless of source, and nothing stops a template from authoring periodConfig and twoFigureConfig together. The comp lane is explicitly in flux (phrase/passage strategies exempted), so this is design debt to schedule, not a defect to patch now.

Recommendation: When the strategy set settles: `std::variant<GenerateSpec, ReferenceSpec, TransformSpec, LockedSpec, LiteralSpec>` for FigureTemplate content; `std::variant<std::monostate, PeriodPhraseConfig, SentencePhraseConfig, TwoFigurePhraseConfig, ElaboratedPhraseConfig>` keyed by strategy; `enum class CadenceType { None, Half, Full }` shared by figure and phrase; TransformOp for rhythm/contour transforms. Serialization then collapses to a `type` tag + per-alternative object.

- verifier: Evidence confirmed: FigureTemplate 110-174 is a source-dependent union (~30 fields grouped by '--- For Generate/Reference/Locked/Literal ---' comments); shapeDirection int (129), direction enum (132), shape enum (128); rhythmTransform/contourTransform strings (155,157); figureCadenceType magic int (168) and PhraseTemplate.cadenceType (302); 'Only one is populated at a time' at 327-328. composer.h:1057 reads shapeDirection — confirmed. templates_json.h to_json(FigureTemplate) 314-376 guards Generate/Reference/Transform/Locked groups by source but emits shape/motif-atom fields unconditionally (354-365) — confirmed. from_json(PhraseTemplate) 714-733 loads every config present with no exclusivity check, and each strategy reads only its own optional (phrase_strategies.h:40/93, two_figure_phrase_strategy.h:35, elaborated_phrase_strategy.h:44), so co-authored configs are silently ignored — confirmed. Nit: 'duplicate the TransformOp enum' is only partial (TransformOp has Invert/Reverse/Stretch/Compress but no Expand/Contract). Severity low rather than medium: the finding itself says 'design debt to schedule, not a defect'; the Period/Sentence/TwoFigure/Elaborated strategies and the FigureSource::Generate path are all marked SHELVED in docs/autonomy/comp/GENERATORS.md; a variant refactor of this data model is exactly what Matt's 'discuss before refactoring' rule gates. Heavy overlap with F129 (same PhraseTemplate optionals).
- verifier: All citations verified: FigureTemplate 110-174 has 31 fields grouped by source-mode comments; shapeDirection int :129, direction enum :132, shape enum :128, rhythmTransform string :155, figureCadenceType :168 and PhraseTemplate.cadenceType :302 both magic ints; 'Only one is populated at a time' at :327-328 over the four optionals :329-332. composer.h:1057 does read ft.shapeDirection (`int approachDir = (ft.shapeDirection < 0) ? -1 : 1;`). templates_json.h to_json(FigureTemplate) at 314-376 guards the Generate/Reference/Transform/Locked groups by source but emits shape/shapeDirection/shapeParam/direction and rhythm/contour motif-atom fields unconditionally, as claimed. from_json(PhraseTemplate) parses periodConfig/sentenceConfig/twoFigureConfig/elaboratedConfig independently, so authoring two together is accepted. Partial overstatement: TransformOp (figure_transforms.h:10-30) has Reverse/Stretch/Compress/Invert matching rhythmTransform strings, but contourTransform's 'expand'/'contract' have no TransformOp counterpart, so 'duplicate' is only partial. No runtime defect, serializer cost only; the finding itself says 'design debt to schedule, not a defect', and the comp lane is in flux (strategies shelved 09-21), so this is a style/design preference with no concrete cost today. Overlaps F129 on the PhraseTemplate configs.

## F128 [CONFIRMED] low (reporter: low) workaround-hack — engine/include/mforce/music/templates.h:305

**Cadential 'held' arrival — a self-described 2026-04 workaround — is still the default PhraseTemplate behaviour**

Reporters: arch-hacks-census

Evidence: `templates.h:309-311 `//              tonic is sustained but nothing approaches it. This is the
    //              standing workaround from 2026-04, kept as the default
    //              because replacing it is a taste question.` :316 `// Empty = "held".`; docs/autonomy/comp/REVIEW.md:158-160 'That workaround predates `apply_cadence` growing a real tail rebuild, and a single note defeats it'.`

Why: The comp lane's own measurement (REVIEW 18) shows the workaround neither removes the leap it was blamed for nor delivers the longer arrival it exists for, yet every template without an explicit cadentialArrival still gets it. Documented, gated on Matt's listen, but the default means new templates inherit a known-defeated mechanism.

Recommendation: Once REVIEW 18 is verdicted, flip the default (or make cadentialArrival required) and delete the 'held' branch's special handling in build_approach_steps.

- verifier: Evidence verbatim at templates.h:305-317 ('standing workaround from 2026-04, kept as the default because replacing it is a taste question', 'Empty = held'). REVIEW.md:158-160 quote confirmed; REVIEW 18 (lines 153-175) is still open and 'Verdict decides the default'. Default path confirmed: composer.h:1699-1704 and period_passage_strategy.h:152 pass cadentialArrival == "approach" into DefaultFigureStrategy::choose_shape (default_strategies.h:75-79, approachArrival=false keeps HeldNote). Scope nuance: the default only bites phrases with function != Free whose Generate figures have shape Free, not 'every template'. The finding misreads one REVIEW 18 anti-result — the leap is what the workaround was blamed for CAUSING, and REVIEW says it does not occur in either arm; the second anti-result (held arm is SHORTER, 1.00/0.91 vs 1.54/1.74 beats) is quoted correctly. Not a code-review action item: it is tracked as BACKLOG item 9 ('default unchanged (taste, REVIEW 18)') awaiting Matt's listen, and apply_cadence + cadentialArrival shaping is SHELVED under the 2026-09-21 crawl reset (GENERATORS.md:24).
- verifier: Comment text at templates.h:309-311 and :316 is verbatim as quoted. docs/autonomy/comp/REVIEW.md:158-160 is verbatim as quoted; the supporting measurement bullets are at 163-168 ('The leap ... does not happen. Both arms enter the final note by +1 semitone', 'held arm does not deliver the longer arrival ... 1.00 / 0.91 beats held, against 1.54 / 1.74 approach'). Default path confirmed: DefaultFigureStrategy::choose_shape(default_strategies.h:206-212) returns HeldNote unless approachArrival, default false (:75-79); callers pass `phraseTmpl.cadentialArrival == "approach"` at composer.h:1704 and period_passage_strategy.h:152; from_json defaults cadentialArrival to empty (templates_json.h:709, :816). So every template without explicit cadentialArrival gets held. REVIEW 18 is still tagged [listen] with no verdict entry, so the finding restates an already-open, Matt-gated item; nothing actionable until his verdict (and flipping the default breaks the bit-identical golden renders the composer comments guard). Side observation: the header comment at :307-309 ('LEAPS straight onto the target') is itself contradicted by the REVIEW 18 measurement, so that comment is stale regardless of verdict.

## F129 [CONFIRMED] low (reporter: high) smell — engine/include/mforce/music/templates.h:470

**Strategy configuration is hard-wired into the core template structs: PassageTemplate is a union of every strategy's fields and PhraseTemplate carries four mutually exclusive optional configs**

Reporters: arch-music-model

Evidence: `templates.h:329-332 `std::optional<PeriodPhraseConfig> periodConfig; std::optional<SentencePhraseConfig> sentenceConfig; std::optional<TwoFigurePhraseConfig> twoFigureConfig; std::optional<ElaboratedPhraseConfig> elaboratedConfig;` ('Only one is populated at a time'); PassageTemplate 470-577 holds chordConfig, chordProgression, realizationStrategy, rhythmPattern, voicingSelector, voicingProfile, voicingProfileSelector, voicingProfileSelectorConfig, voicingDictionary, libraryConfig, melodyPassageFile, melodyOctave, anchorMode, melodyProfile, pedalBuildupConfig, sequenceConfig, connectiveConfig, wanderingConfig. Each needs hand-written JSON twice: templates_json.h:934-964 and 1094-1145 (four near-identical blocks). The codebase already has the right pattern once: 545 `nlohmann::json voicingProfileSelectorConfig; // Opaque to the template layer; each selector parses its own shape.``

Why: Adding a strategy means editing templates.h, two JSON sites and the Composer constructor -- the registry is not an extension point, it is a lookup table in front of a hard-coded schema. The exempt strategy files are forced to be awkward by this: their configs cannot live with them. The struct is also ~1 KB per passage of mostly-empty optionals copied in every `PassageTemplate planned = s->plan_passage(...)` (composer.h:264) and `PhraseTemplate localTmpl = phraseTmpl` (1403) copy.

Recommendation: Replace the per-strategy fields with one `std::variant<...>` of config structs (closed set, compile-checked) or an opaque `nlohmann::json strategyConfig` parsed by the strategy that owns it (the voicingProfileSelectorConfig pattern); let registry entries expose `parse_config(const json&)`. Keep only cross-cutting fields (name, startingPitch, phrases, strategy, seed, locked, endGrid, scaleOverride) on the template.

- verifier: Evidence confirmed: PhraseTemplate optionals at 329-332; PassageTemplate 470-577 carries every listed field (chordConfig 510, chordProgression 518, realizationStrategy 523, rhythmPattern 524, voicingSelector 529, voicingProfile 535, voicingProfileSelector 540, voicingProfileSelectorConfig 545, voicingDictionary 550, libraryConfig 553, melodyPassageFile 558, melodyOctave 559, anchorMode 567, melodyProfile 569, pedal/sequence/connective/wandering configs 573-576). templates_json.h 934-964 (to_json) and 1094-1145 (from_json) are four near-identical per-config blocks — confirmed. Composer ctor registers all strategies (composer.h:164-225), so adding one does touch templates.h + two JSON sites + the ctor — consequence holds. composer.h:264 and :1403 copies exist but are compose-time, once per passage/phrase, never on a render path — the '~1 KB copied' cost is not a real cost. Severity low not high: (1) explicit registries are a CLAUDE.md requirement, so 'the registry is not an extension point' is not a defect; (2) every passage strategy named (pedal/sequence/connective/wandering, period, library, voicing selectors) is SHELVED per docs/autonomy/comp/GENERATORS.md; (3) the recommendation to add an opaque nlohmann::json strategyConfig to the template directly contradicts F126's recommendation to remove json from templates.h — the two findings cannot both be applied; (4) this duplicates F127's PhraseTemplate observation.
- verifier: Evidence verified: four optionals at :329-332 with the 'Only one is populated' comment; every PassageTemplate field listed exists at :510-576; opaque `nlohmann::json voicingProfileSelectorConfig` at :545 with the quoted comment. Both JSON sites exist and are near-identical per-config blocks: to_json at templates_json.h:934-964 and from_json at 1094-1145 (pedalBuildup/sequence/connective/wandering). Registration is inside `explicit Composer(uint32_t seed ...)` at composer.h:164-219 (register_phrase 179-183, register_passage 212-219), so 'templates.h + two JSON sites + Composer constructor' is accurate. Weak sub-claim: composer.h:264 `PassageTemplate planned = s->plan_passage(...)` is a by-value return (moved/elided, not copied); the real copies are :269 (`passages[sectionName] = planned`) and :1403 (`PhraseTemplate localTmpl = phraseTmpl`), both in the offline compose phase, not a render path, so the ~1 KB copy cost is immaterial. 'The registry is a lookup table' describes the explicit registry CLAUDE.md requires, not a smell. Net: an accurate description of an extensibility/maintenance smell (four ~10-line edit sites per new strategy) with no correctness or runtime cost, in a lane whose strategies are currently shelved; 'high' is not supported. Overlaps F127.

## F130 [CONFIRMED] low (reporter: low) smell — engine/include/mforce/music/templates.h:732

**add_derived_motif re-implements find_motif inline and carries stale plan-step comments that contradict the code**

Reporters: music-json

Evidence: `const Motif* parent = nullptr;
for (const auto& m : motifs) { if (m.name == parentName) { parent = &m; break; } }   // 732-735, == find_motif() at 658-661
// Synthesize content per transform. Task 7's skeletal coverage: Invert,
// Reverse only. Other transforms throw; Task 10 extends.   // 751-752, while the switch handles VarySteps/VaryRhythm/RhythmTail at 773-802`

Why: The helper exists ten lines above and is not used; the comments (also 705-707) describe an intermediate plan state, so a reader is told the function supports two ops when it supports five. Low cost, but it is the kind of drift that makes the header untrustworthy as documentation — and this header's comments are otherwise one of its strengths.

Recommendation: `const Motif* parent = find_motif(parentName);` and rewrite the two comments to describe the current switch.

- verifier: Confirmed exactly. templates.h:732-735 hand-rolls the same linear name lookup as PieceTemplate::find_motif at 658-661 (const member returning const Motif*, callable from the non-const add_derived_motif with no change). Comments at 705-707 and 751-752 claim Invert + Reverse only with 'Task 10 extends', while the switch at 760-807 already handles Invert, Reverse, VarySteps, VaryRhythm and RhythmTail. Pure cleanup; the fix in the recommendation is correct.
- verifier: Verified verbatim: templates.h:732-735 hand-rolls the same linear name lookup as find_motif at :658-661 (const member, callable from the non-const add_derived_motif, returns const Motif* matching the local's type). Comments at :705-707 and :751-752 say Invert + Reverse only / 'Task 10 extends', while the switch at :760-807 handles Invert, Reverse, VarySteps (:773), VaryRhythm (:782), RhythmTail (:789). Pure comment drift plus a 4-line duplicate; no behavioral consequence.

## F131 [CONFIRMED] low (reporter: medium) duplication — engine/include/mforce/music/templates_json.h:33

**parse_chord_progression re-implements from_json(ScaleChord) including the VoicingPin block, with a different spelling of the quality default**

Reporters: music-json

Evidence: `sc.quality = &ChordDef::get(entry.value("quality", std::string("Major")));   // templates_json.h:37
if (entry.contains("pin")) { ... pin.inversion = jp.value("inversion", 0); pin.spread = ...; pin.topTone = jp.value("topTone", -1); }  // 42-50
--- music_json.h:289-298 ---
std::string q = j.value("quality", std::string("M"));
sc.quality = &ChordDef::get(q);
if (j.contains("pin")) { ... identical 7 lines ... }`

Why: Two copies of the per-chord parse; the flat form is just an array of ScaleChord objects plus a "beats" field. "Major" and "M" both resolve to the same ChordDef (chord.cpp:46-51 matches `name` as well as `shortName`), so this is not a bug today, but the next pin field or quality alias has to be added twice and the two defaults will drift.

Recommendation: `for (const auto& entry : j) { ScaleChord sc; from_json(entry, sc); prog.add(sc, entry.at("beats").get<float>()); }`.

- verifier: Verified. templates_json.h:33-52 re-implements the per-ScaleChord parse (degree/alteration/quality + the 7-line pin block at 42-50) that music_json.h:286-302 already provides; the pin blocks are identical apart from music_json's `else sc.pin.reset()` (a no-op on a fresh ScaleChord). Quality default differs ('Major' vs 'M') but chord.cpp:46-51 resolves both to the same def: 'M' -> get("") and 'Major' matches `cd.name` on the {"","","Major",...} entry at chord.cpp:13. The recommended `from_json(entry, sc); prog.add(sc, entry.at("beats"))` is behavior-identical. No bug today; a ~15-line dup with a one-line fix, so low.
- verifier: Verified. templates_json.h:33-52 hand-parses ScaleChord with default quality "Major" (line 37) and a pin block at 42-50 that is verbatim music_json.h:291-298 (music_json.h:286-302 is from_json(ScaleChord), default "M" at 289). chord.cpp:46-51: get("M") -> get(""), and the loop matches cd.shortName or cd.name; s_chordDefs[0] is {"","","Major",...} (chord.cpp:13), so both defaults resolve to the same ChordDef - not a bug today, as the finding says. Recommendation is sound: from_json(ScaleChord) ignores the extra "beats" key, so `from_json(entry, sc); prog.add(sc, entry.at("beats"))` is a drop-in. Severity low: ~9 duplicated lines, identical behavior, one-line fix.

## F132 [CONFIRMED] low (reporter: medium) duplication — engine/include/mforce/music/templates_json.h:95

**Ten enums hand-serialized as paired switch + if/else chains (~250 lines) when the vendored nlohmann 3.12 generates both directions from one table**

Reporters: music-json

Evidence: `inline void to_json(json& j, FigureShape s) { switch (s) { case FigureShape::Free: j = "free"; ... 17 cases } }
inline void from_json(const json& j, FigureShape& s) { if (str == "scalar_run") ... 16 else-ifs ... }
// same shape for FigureSource 95-112, TransformOp 114-147, MotifRole 150-171, MotifOrigin 173-188, PartRole 190-209, MelodicFunction 215-231, FigureDirection 283-308, Method/ChoiceMode 592-622, PeriodVariant 765-778; music_json.h Articulation 19-67 (17 branches x2), Dynamic 147-159, PitchSelectionType 162-187`

Why: Every enum value is spelled in two places that must agree; the string table is the only thing that matters and it is duplicated. This is the structural cause of the inconsistent unknown-value handling (previous finding) and of the inline one-offs like stepMode (368/427-431) and TwoFigurePhraseConfig::Method (561-579) that did not even get their own functions. music_json.h:148/163 index `names[int(d)]` with no static_assert on array size vs enum count (currently 8/8 and 18/18 — verified, but unguarded).

Recommendation: `NLOHMANN_JSON_SERIALIZE_ENUM(FigureShape, {{FigureShape::Free, "free"}, ...})` per enum — one table, both directions, and unknown input maps to the first entry which can be a sentinel you then reject. Keep a static_assert(std::size(names) == N) wherever an indexed table remains.

- verifier: Verified. Paired switch + if/else chains exist at the cited ranges (FigureSource 95-112, TransformOp 114-147, MotifRole 150-171, MotifOrigin 173-188, PartRole 190-209, MelodicFunction 215-231, FigureShape 237-277 with 17 cases / 16 else-ifs, FigureDirection 283-308, ElaboratedPhraseConfig::Method+ChoiceMode 592-622, PeriodVariant 765-778 — 11 enums, not 10, since Method and ChoiceMode are separate). Inline one-offs stepMode (368/427-431) and TwoFigurePhraseConfig::Method (561-579) confirmed. music_json.h:148 and 163 index `names[int(..)]` with no static_assert; counts match today (Dynamic 8/8 at structure.h:18, PitchSelectionType 18/18 at figures.h:513-519). Vendored nlohmann is 3.12.0 (json.hpp:68-70) and NLOHMANN_JSON_SERIALIZE_ENUM is defined at json.hpp:2586. Caveats: Articulation (music_json.h:19-67) is a std::variant, not an enum, so the macro does not apply there; and the macro's unknown->first-entry semantic means getting throw-on-unknown (F133) requires adding a sentinel enumerator to every enum, a real design cost. Style/duplication only; the concrete failure mode belongs to F133, so low on its own.
- verifier: Verified. Paired switch/if-else chains at FigureSource 95-112, TransformOp 114-147, MotifRole 150-171, MotifOrigin 173-188, PartRole 190-209, MelodicFunction 215-231, FigureShape 237-277 (17 cases / 16 else-ifs), FigureDirection 283-308, ElaboratedPhraseConfig::Method 592-606 and ChoiceMode 608-622, PeriodVariant 765-778 - eleven enums in this file, not ten. Vendored nlohmann is 3.12.0 (engine/third_party/nlohmann/json.hpp:68-70) and defines NLOHMANN_JSON_SERIALIZE_ENUM at :2586. music_json.h:148 and :163 index names[int(x)]; Dynamic (structure.h:18) has 8 values vs 8 names, PitchSelectionType (figures.h:513-519) 18 vs 18; no static_assert anywhere in music_json.h. Two caveats on the evidence/recommendation: Articulation (music_json.h:19-67) is a std::variant whose Bend/Slide alternatives carry payload, so SERIALIZE_ENUM cannot replace it; and the proposed sentinel-then-reject approach adds a value to every enum, which touches every exhaustive switch over them. Consequence (two-place edits, drift, root of the F133 inconsistency) follows. Medium: worth fixing, no behavior defect by itself.

## F133 [CONFIRMED] medium (reporter: high) soundness — engine/include/mforce/music/templates_json.h:111

**Six enum loaders silently map unknown strings to a default while four throw — a typo in an authored template changes the composition instead of failing the load**

Reporters: music-json

Evidence: `else s = FigureSource::Generate;        // 111
else t = TransformOp::None;             // 146
else r = PartRole::Melody;              // 208
else f = MelodicFunction::Free;         // 230
else s = FigureShape::Free;             // 276
else d = FigureDirection::Ascending;    // 307
vs.
else throw std::runtime_error("Unknown MotifRole: " + s);  // 170 (also 187, 605, 621, 777)`

Why: `"shape": "scalar_rum"` or `"transform": "invert "` loads as Free/None and the piece renders differently with no diagnostic; the same pattern exists for stepMode (430: anything but "chordTone" is Scale), TwoFigurePhraseConfig::Method (579), Articulation type (music_json.h:66 -> Default) and Ornament type (music_json.h:144 -> none). The file has no single contract for bad input, so an author cannot know whether a mistake will be caught. Given the project history of template-authoring traps (connectors note at templates.h:294-297), silent acceptance is the expensive failure mode.

Recommendation: Throw on unknown strings everywhere, including the offending value and the enum name in the message. Doing this via one table per enum (see the NLOHMANN_JSON_SERIALIZE_ENUM finding) makes the contract uniform by construction; if a lenient path is wanted for legacy files, make it an explicit opt-in, not the default.

- verifier: Verified line-for-line: silent defaults at 111 (Generate), 146 (None), 208 (Melody), 230 (Free), 276 (Free), 307 (Ascending); throws at 170, 187, 605, 621, 777; stepMode fallthrough at 429-430; TwoFigurePhraseConfig::Method at 579; Articulation -> Default at music_json.h:66; Ornament -> none at music_json.h:144; connectors trap note at templates.h:294-297. Consequence traced: from_json(FigureTemplate) 393/397 feeds these loaders, so `"shape":"scalar_rum"` loads as Free with no diagnostic. Mitigations that lower but do not remove the cost: (1) `--lint-template` (tools/mforce_cli/main.cpp:1071-1144) round-trips and would flag such a typo as DROPPED/CHANGED, but it is opt-in; (2) a grep of scores/ shows every enum string in use today is valid, so nothing is currently mis-loading. Also note FigureShape and MelodicFunction loaders have NO explicit 'free' branch — the else IS how 'free' parses — so throw-on-unknown must add those branches. Latent but a one-character authoring trap in hand-authored templates with a documented history of exactly this class; high.
- verifier: Verified. Silent fallbacks at exactly 111 (Generate), 146 (None), 208 (Melody), 230 (Free), 276 (Free), 307 (Ascending). Throwing loaders at 170, 187, 605, 621, 777 - that is FIVE, not the title's "four" (the evidence list is correct). stepMode 427-431 (StepMode has only Scale/ChordTone, templates.h:78, so any typo of "chordTone" yields Scale), TwoFigurePhraseConfig::Method 575-579, Articulation music_json.h:66 -> Default, Ornament music_json.h:144 -> Ornament{} (object path only; the legacy string path at :123 uses .at() and throws). Consequence traced: from_json(FigureTemplate) calls from_json(j.at("shape"), ft.shape) at 393 and transform at 390, so "scalar_rum" loads as Free and "invert " as None with no diagnostic. templates.h:294-297 connectors REPEAT-OFFENDER warning confirms the authoring-trap history. Concrete-cost check: I tallied every shape/transform/direction/source/function/role/origin/variant/choiceMode/buildMethod string across scores/**/*.json; all are valid today, so nothing is currently mis-loading. Medium: real latent hazard, trivial fix, no present victim - becomes high the first time an author typos.

## F134 [REFUTED] low (reporter: medium) duplication — engine/include/mforce/music/templates_json.h:692

**JSON sub-object parsers duplicated instead of from_json overloads: PhraseTemplate vs PeriodSpec phrase, VoicingProfile ×4, anchor config ×2**

Reporters: arch-duplication

Evidence: `templates_json.h from_json(PhraseTemplate) 692-759 vs PeriodSpec loadPhrase 799-840 (connectors block 741-758 vs 821-838 verbatim; comment 794-798 claims the PhraseTemplate path "enforces startingPitch" but 700-703 reads it as optional — stale rationale for the fork); VoicingProfile parsed at random_voicing_profile_selector.h:26-50, drift_voicing_profile_selector.h:33-59 (verbatim), scripted_voicing_profile_selector.h:26-44 and templates_json.h:1048-1071 — no `from_json(VoicingProfile)` exists; anchor-config blocks 934-964 vs 1094-1145.`

Why: Four owners of the VoicingProfile JSON schema and two of the phrase schema; a new key is accepted by whichever parser the author remembered. The comment justifying the PhraseTemplate/PeriodSpec fork is no longer true, so the fork has no reason to exist.

Recommendation: Add `from_json(VoicingProfile&)` in voicing_profile.h and `from_json(PhraseTemplate&)` as the single phrase reader; PeriodSpec and the selectors call them. Delete the stale comment.

- verifier: Two of the three headline claims do not survive inspection. (a) PhraseTemplate vs PeriodSpec loadPhrase duplication, verbatim connectors blocks 741-758 vs 821-838, and the stale 'enforces startingPitch' comment at 794-798 vs optional read at 700-703: TRUE, but this is F135 (dup_count 4). (b) 'VoicingProfile x4': random_voicing_profile_selector.h:26-50 and drift_voicing_profile_selector.h:33-59 do NOT parse VoicingProfile — they parse selector config {priorityMin, priorityMax, inversionProfiles: [[int]], spreadProfiles: [[int]]}; random 31-49 and drift 40-58 are verbatim copies of each other but of a different schema, so from_json(VoicingProfile) could not replace them. Only scripted_voicing_profile_selector.h:29-42 and templates_json.h:1048-1060 parse VoicingProfile objects, and no from_json(VoicingProfile) exists (grep confirmed). Those two HAVE drifted: scripted reads priority/allowedInversions/allowedSpreads only, while templates_json also reads repeatPenalty and cadential, which smooth_voicing_selector.h:80 and :151 consume — so `"cadential": true` in a scripted sequence entry is silently ignored. That is a real, narrower finding the batch does not state. (c) 'anchor config x2' at 934-964 vs 1094-1145 is to_json vs from_json of the four passage configs (the two serialization directions), not two copies of one parser. As written the finding is not supported; the salvageable content is the scripted-selector VoicingProfile drift.
- verifier: Refuted as stated. The PhraseTemplate-vs-loadPhrase half is true (stale comment 794-798 vs optional startingPitch at 700; connectors 741-758 == 821-838) but that is F135. The "VoicingProfile x4" claim is wrong: random_voicing_profile_selector.h:26-50 and drift_voicing_profile_selector.h:33-59 are configure_from_json bodies that parse the SELECTOR config schema (priorityMin, priorityMax, inversionProfiles/spreadProfiles as list-of-lists; drift adds priorityStepMax/profileTransitionProb) and construct a VoicingProfile at runtime in profile_for_chord (random :54-68, drift :88-95). They never read priority/allowedInversions/allowedSpreads from JSON. Only scripted_voicing_profile_selector.h:29-42 and templates_json.h:1048-1071 parse a VoicingProfile object, and they are not the same (scripted omits repeatPenalty and cadential). So "four owners of the VoicingProfile JSON schema" is false - it is two, with differing coverage. The "anchor config x2" (934-964 vs 1094-1145) is a to_json/from_json pair, not two parsers of one schema; that is F136's point. Confirmed only that no from_json(VoicingProfile) exists (grep across engine/ and tools/). What survives that no other finding covers: random/drift duplicate each other's ~18-line list-of-lists parse, and scripted silently drops cadential/repeatPenalty on a sequence profile. Low.

## F135 [CONFIRMED] medium (reporter: high) duplication — engine/include/mforce/music/templates_json.h:799

**from_json(PeriodSpec) carries a 42-line copy of from_json(PhraseTemplate) that has already drifted and silently drops strategy configs**

Reporters: arch-build-headers, arch-modern-cpp, arch-music-model, music-json

Evidence: `// instead we read each phrase's fields manually here.
    auto loadPhrase = [](const json& pj) {
        PhraseTemplate ph;
        ...
        if (pj.contains("connectors")) { ... }   // 821-838 == 741-758 verbatim
        return ph;
    };`

Why: The justifying comment at 794-798 says from_json(PhraseTemplate) 'enforces startingPitch', but line 700 reads `if (j.contains("startingPitch"))` — it is optional there, so the stated reason is stale and the lambda is pure duplication. It has drifted: the copy never reads periodConfig/sentenceConfig/twoFigureConfig/elaboratedConfig (692-733 does), so a period antecedent/consequent authored with a phrase strategy config loses it on load with no error; the copy makes "figures" optional while the real function requires it (`j.at("figures")` at 694 — which is why baseline scores write `"figures": []` boilerplate, test_period_sentence.json:23,58). The connectors block (741-758 vs 821-838) is a second verbatim copy including the dense-array warning comment.

Recommendation: Delete loadPhrase; write `from_json(j.at("antecedent"), ps.antecedent); from_json(j.at("consequent"), ps.consequent);`. If figures should be optional for strategy-driven phrases, make that change once in from_json(PhraseTemplate) (`if (j.contains("figures"))`). Extract the connectors parse into `from_json(const json&, std::optional<FigureConnector>&)`.

Also reported as: PeriodSpec's loadPhrase lambda re-implements from_json(PhraseTemplate) on a justification that is no longer true | from_json(PeriodSpec) re-implements from_json(PhraseTemplate) on a false premise, and VoicingPin parsing is duplicated across the two JSON headers | loadPhrase lambda re-implements from_json(PhraseTemplate); connector parsing is verbatim in both

- verifier: Verified. Comment 794-798 says from_json(PhraseTemplate) 'enforces startingPitch'; 700-703 reads it as optional, so the rationale is stale. loadPhrase 799-840 reads name/startingPitch/parallel/totalBeats/cadenceType/cadenceTarget/cadentialArrival/function/seed/locked/strategy/connectors and omits periodConfig/sentenceConfig/twoFigureConfig/elaboratedConfig that 714-733 reads; connectors 821-838 == 741-758 verbatim including the warning comment; 694 `j.at("figures")` is required while 801 makes it optional; test_period_sentence.json:23,58 are `"figures": []` on phrases[] entries with strategy period_phrase/sentence_phrase (also test_elaborated.json:29,52). Consequence traced: period_passage_strategy.h:232-281 copies p.antecedent/p.consequent into seed.phrases and compose_passage 311-314 resolves phrase.strategy per phrase, so a config authored on a period phrase is dropped at load. Nuances: the loader is silent, but the strategy then prints '<X>Config is empty; returning empty phrase' to stderr (phrase_strategies.h:40-43, 93-96; two_figure_phrase_strategy.h:35-39; elaborated_phrase_strategy.h:44-48) — a misleading diagnostic rather than none; no score in scores/ currently authors a strategy config inside periods[] (grep), so the drop is latent; --lint-template would report it as DROPPED. Real verbatim dup with demonstrated drift, but the authoring trap is latent and partially diagnosed downstream: medium rather than high.
- verifier: Verified at 794-842. Comment 794-798 says from_json(PhraseTemplate) "enforces startingPitch" but 700-703 reads it with j.contains - stale. loadPhrase 799-840 vs 692-759: figures optional at 801 vs j.at("figures") at 694; the four config blocks at 714-733 (periodConfig/sentenceConfig/twoFigureConfig/elaboratedConfig) have no counterpart in the lambda; strategy IS read (820); connectors 821-838 are verbatim 741-758 including the dense-array comment. test_period_sentence.json:23 and :58 are top-level phrases with strategy + "figures": [] (and test_elaborated.json:29,:52), consistent with j.at("figures") forcing the boilerplate. Consequence traced further than the finding did: period_passage_strategy.h:232/250/280 copy p.antecedent wholesale into seed.phrases (strategy survives, configs are already gone), :311-313 dispatch on localTmpl.strategy, and phrase_strategies.h:40-44 / :93-97, two_figure_phrase_strategy.h:35-46, elaborated_phrase_strategy.h:44-55 then find the optional empty and return an empty phrase with a std::cerr line. So "no error" is slightly overstated - load is silent, realize prints a misleading diagnostic blaming a config the author did write - but the substance (silent drop, wrong output) holds. No tracked score exercises it: files with "periods" (period_markov_test.json, test_k467_period.json) and files with phrase configs (test_elaborated.json, test_period_sentence.json) do not overlap. Medium: drift has already materialized in two ways plus a false comment, failure path confirmed but unexercised; would be high once any score authors a strategy phrase inside a period.

## F136 [CONFIRMED] low (reporter: medium) duplication — engine/include/mforce/music/templates_json.h:934

**Passage config structs are field-listed inline in both directions of PassageTemplate (~110 lines) instead of having their own serializers**

Reporters: music-json

Evidence: `j["pedalBuildupConfig"] = json{{"levels", c.levels}, {"climbStep", c.climbStep}, {"cellBeats", c.cellBeats}, ...};   // 936-940
...
c.levels    = jc.value("levels", c.levels);
c.climbStep = jc.value("climbStep", c.climbStep);   // 1097-1103, repeated for sequence 1110-1116, connective 1123-1130, wandering 1137-1143`

Why: PedalBuildupConfig/SequencePassageConfig/ConnectivePassageConfig/WanderingPassageConfig (plus LibraryPassageConfig 921-927/1080-1087 and RhythmPattern 881-895/1025-1041) are serialized by hand inside to_json/from_json(PassageTemplate), making those two functions 110 and 172 lines and putting rangeCap/maxTries/seed/cellBeats in eight places. ChordAccompanimentConfig (58-62, 967-973) shows the file's own better convention. The `c.x = jc.value("x", c.x)` idiom is exactly what NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (3.11+; 3.12 vendored) generates. Adding a field to any config today means editing templates.h plus two blocks here.

Recommendation: One `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PedalBuildupConfig, levels, climbStep, cellBeats, holdBeats, rangeCap, maxTries, seed)` per config next to the struct (or hand-written to_json/from_json pairs if terse-omit-defaults output is required), then `if (pt.pedalBuildupConfig) j["pedalBuildupConfig"] = *pt.pedalBuildupConfig;` in PassageTemplate. Same for RhythmPattern and LibraryPassageConfig.

- verifier: Verified. to_json(PassageTemplate) 856-965 is 110 lines and from_json 975-1146 is 172; the four passage configs are field-listed inline at 934-964 and 1094-1145 (rangeCap/maxTries/seed/cellBeats appear in 8 places); LibraryPassageConfig at 921-927/1080-1087 and RhythmPattern at 881-895/1025-1041; ChordAccompanimentConfig has its own pair at 58-62/967-973. Field lists match the structs in templates.h:412-464 exactly, so there is no drift today. Vendored nlohmann is 3.12.0 and NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT is at json.hpp:2823; its generated `value("x", default.x)` matches the idiom at 1097-1103, and since the four to_json blocks already emit every field unconditionally, the macro is a drop-in for those four. Caveats: moving the code into per-struct to_json/from_json pairs (first half of the recommendation) does not reduce edit sites, only the macro does; and LibraryPassageConfig/RhythmPattern to_json are terse (omit defaults), which the macro would change. No concrete cost today beyond editing two blocks per new field: low.
- verifier: Verified. to_json(PassageTemplate) 856-965 is 110 lines, from_json 975-1146 is 172 lines. Inline config blocks at 934-964 (to) and 1094-1145 (from) for PedalBuildup/Sequence/Connective/Wandering; rangeCap/maxTries/seed/cellBeats appear in all four structs x two directions = eight places. LibraryPassageConfig 921-927 / 1080-1087 and RhythmPattern 881-895 / 1025-1041 likewise inline; ChordAccompanimentConfig has its own pair at 58-62 / 967-973. Structs at templates.h:412-464 all carry default member initializers, so `c.x = jc.value("x", c.x)` is exactly NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT semantics (defined in the vendored 3.12.0 header at json.hpp:2823), and the current to_json already writes every field unconditionally, so the macro would not change the emitted JSON. Consequence (three-place edit per new field) follows. Medium: pure maintainability, no behavior defect, but ~280 lines the library generates.

## F137 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/music/templates_json.h:1035

**const json::operator[] used on keys that are not checked — JSON_ASSERT in debug, end-iterator dereference (UB) in release on malformed input**

Reporters: music-json

Evidence: `for (auto& b : ov["bars"]) bo.bars.push_back(b.get<int>());        // 1035
for (auto& v : ov["pattern"]) bo.pattern.push_back(v.get<float>()); // 1036
ctx.beat = kc["beat"].get<float>();                                 // 1219
std::string keyName = kc["key"].get<std::string>();                 // 1220`

Why: `ov` and `kc` are `const json&` (loops at 1033 and 1217 over const containers). nlohmann 3.12 json.hpp:22182-22189 implements const operator[] as `JSON_ASSERT(it != end()); return it->second;` — a rhythmPattern override missing "bars", or a keyContext missing "beat"/"key", is an assert in debug and undefined behaviour in a release build instead of a `json::out_of_range` naming the key. The same file uses `.at()` for the identical purpose two lines away (1030 `jrp["defaultPattern"]` is guarded, 1055 uses `.at`), and mixes guarded `j["x"]` (1012, 1019, 1024, 1027, 1207, 1212, 1232) with `.at()` throughout, so there is no rule a reader can rely on.

Recommendation: Use `.at("key")` for every required key and `.value("key", default)` / `contains` for optional ones; never const `operator[]` in a loader. A one-line clang-tidy-style grep for `\["` in the two *_json.h files finds all sites.


## F138 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/music/templates_json.h:1179

**to_json(SectionTemplate) omits styleName, which from_json reads and Composer consumes — engine round trip drops the style table**

Reporters: music-json

Evidence: `inline void to_json(json& j, const PieceTemplate::SectionTemplate& sd) {
    j = json{{"name", sd.name}, {"beats", sd.beats}};
    if (!sd.keyName.empty()) ...
    if (!sd.progressionName.empty()) ...
    if (sd.chordProgression) ...
    if (!sd.keyContexts.empty()) { ... }
}                                   // no styleName
...
if (j.contains("styleName")) { sd.styleName = j["styleName"]...   // 1231`

Why: styleName is live: composer.h:873-874 and period_passage_strategy.h:340-342 call `StyleTable::load_by_name(sd->styleName)`. A template that goes through the engine and is written back (the `--lint-template` path that caught the identical chordConfig omission, per the comment at 871-873) comes out without its ChordWalker style, exactly the class of bug the file already documents having fixed once. PieceTemplate::defaultPulse (templates.h:654, consumed at composer.h:63) is the mirror case: serialized in neither direction, so it is unreachable from any authored template and the random-pulse fallback always runs.

Recommendation: Add `if (!sd.styleName.empty()) j["styleName"] = sd.styleName;`. Decide defaultPulse: serialize it or delete the field. A unit test that round-trips every baseline score (`from_json -> to_json -> from_json`, compare dumps) would have caught both and the chordConfig case.


## F139 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/music/templates_json.h:1236

**No schema version on PieceTemplate or Piece JSON; back-compat shims accumulate and unknown keys are silently ignored**

Reporters: arch-music-model

Evidence: `to_json(PieceTemplate) 1236-1249 and from_json 1251-1289 carry no version field; legacy ornament strings music_json.h:116-125 (`// Legacy format: bare string like "MordentAbove"`); flat `voicingPriority`/`allowedInversions` fallback templates_json.h:1061-1070 (`fall back to lifting flat ... for back-compat`); 971-972 `defaultPattern / overrides removed at Stage 11; if present in legacy patches they're silently ignored`; parse_chord_progression accepts two forms 27-54. Every optional is read with `j.value(...)`, so a misspelled key is indistinguishable from an omitted one.`

Why: Without a version the loader cannot tell an old file from a typo, so dead/renamed fields can never be retired safely and each rename adds another permanent branch -- the opposite of the project's stated no-back-compat stance. The run26 report shows the practical cost: six templates rendered silence for months because a missing field defaulted quietly.

Recommendation: Add `"schema": N` at both roots; reject unknown versions; log or reject unknown keys at the top level of each struct (a small allow-list check per from_json); then delete the legacy branches rather than keep them.


## F140 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/render/instrument.h:62

**Instrument::render ignores ctx, re-tests bounds per sample, and implements a whole-timeline render behind a block-shaped MonoSource interface**

Reporters: render-instrument

Evidence: ``void render(const RenderContext& /*ctx*/, float* out, int frames) override` (62) while `int sampleRate{48000};` is a separate member (53); `for (int i = 0; i < len; ++i) { int outIdx = start + i; if (outIdx >= 0 && outIdx < frames) out[outIdx] += rn.samples[i]; }` (68-72); `void add_rendered(float startTime, float* data, int count)` (87) takes a non-const pointer.`

Why: StereoMixer::render calls this exactly once with the full frame count (mixer.cpp 25), so it works, but any block-wise driver (the obvious future JUCE/PortAudio path) would silently misplace every note because there is no running position; and a ctx.sampleRate that differs from the member is silently ignored.

Recommendation: Either assert/clamp on ctx.sampleRate == sampleRate, or drop the member and read ctx. Compute the overlap range `[max(0,start), min(frames, start+len))` once per note. Take `const float*` (or move a vector) in add_rendered. Document on MonoSource that render is whole-timeline, or add a cursor.


## F141 [UNVERIFIED] low (reporter: low) efficiency — engine/include/mforce/render/instrument.h:87

**Offline note buffers are copied into renderedNotes instead of moved, and every note is retained until render() — memory grows with total note-seconds**

Reporters: arch-render-pipeline

Evidence: ``add_rendered(float startTime, float* data, int count) { ... rn.samples.assign(data, data + count); renderedNotes.push_back(std::move(rn)); }` (87-92) called with `buf.data()` from play_note (655) and finish_line (706), where `buf` is a local std::vector that is then discarded; each play_note also `buf.reserve(size_t(std::max(renderSamples, maxSamples)))` (624) = durSamples + 8 s worth of floats per note. Instrument::render (65-73) then sums every note with a per-sample bounds branch.`

Why: Each note's samples are allocated twice (reserve + copy) and the retained set is the sum of all rendered note lengths rather than the timeline length; a few hundred notes with ring-out tails is hundreds of MB transiently. The timeline length is known to callers before the note loop (main.cpp:4388-4396), so accumulation into one timeline buffer is possible.

Recommendation: Pass `std::vector<float>&&` into add_rendered and move; better, give Instrument a `begin_timeline(frames)` so play_note sums directly into the timeline (as capture already does) and render() becomes a copy + volume/clip pass.


## F142 [UNVERIFIED] high (reporter: high) god-class — engine/include/mforce/render/instrument.h:99

**PitchedInstrument is a 620-line all-public header struct carrying ~10 unrelated responsibilities, with trailing-underscore 'private' state that callers reach into**

Reporters: arch-valuesource-graph, render-instrument

Evidence: `One struct owns: pool rotation (187-188), live slot ledger (197-215), offline capture (225-240), streaming handoff (246-373), onset vocabulary (287-310), push-binding delivery (312-347), held-line state machine (382-392, 521-716), ring-out/containment (620-655), glide synthesis (420-429). All members public, including `std::set<std::string> warnedOnsets_;` (295), `HeldLine line_;` (392), `bool warnedNoEnvelopes_{false};` (399). UI reaches into internals: main.cpp 4685 `collect_envelopes(pitched->voicePool[slot].source.get(), ...)`. Same pattern on the collaborator it drives: `std::shared_ptr<ValueSource> trigger_;` is public in envelope.h 159 and read directly at instrument.h 408 `env->trigger_->current()`.`

Why: Every one of the other findings in this unit (lock-forced live path, duplicated ring-out, contract-by-comment streaming, silent continuation drops) is a symptom of one struct absorbing every new spec section as more public members and more branches in play_note. Header-only is fine, but a 620-line header of policy code is recompiled by every translation unit that includes instrument.h (patch_loader, conductor, three tools) and cannot be unit-tested in isolation.

Recommendation: Split by responsibility, keeping Instrument as the thin MonoSource: (1) VoicePool — voicePool, nextVoice, slotInUse, acquire/release; (2) NoteDelivery — apply_note_bindings, deliver_continuation, fire_triggers, make_glide, onset vocabulary (pure functions over VoiceGraph); (3) OfflineRenderer — play_note/finish_line/capture/ring_out writing into Instrument::renderedNotes; (4) LiveVoice — the StreamingVoice handoff. Make state private with narrow accessors; add Envelope::trigger() accessor and drop the public trigger_. Move method bodies to instrument.cpp.

Also reported as: PitchedInstrument is a 620-line header struct mixing nine concerns, with the ring-out/containment loop duplicated inside it


## F143 [UNVERIFIED] medium (reporter: medium) god-class — engine/include/mforce/render/instrument.h:99

**PitchedInstrument is a ~620-line all-public struct mixing voice-pool accounting, offline rendering, held-line state machine, capture, live-continuation and diagnostics**

Reporters: arch-render-pipeline

Evidence: `One `struct PitchedInstrument final : Instrument` holds: pool slot accounting (`slotInUse`, acquire/release 197-215), offline capture buffers (225-240), streaming handoff (246-373), onset vocabulary interning with stderr warnings (287-310), note-binding delivery (312-347), the HeldLine state machine (382-392, 521-716), glide synthesis (420-429), the ring-out renderer (597-656) and `std::fprintf(stderr, ...)` diagnostics at 306, 553, 651, 700. Pseudo-private members are public with trailing underscores (`line_`, `warnedOnsets_`, `warnedNoEnvelopes_`).`

Why: Every caller (UI, CLI, tests) can reach into pool state and line state directly, which is how the UI ended up owning steal policy and envelope gating on top of the engine's own; library code writing to stderr is unusable from a GUI (the UI had to add a MessageBox path for that reason, 4001-4007). The header is also compiled into every TU that touches an instrument.

Recommendation: Split along the existing seams: VoicePool (slots + prepare/continue), NoteRenderer (classic + held-line + ring-out, owning HeldLine), CaptureSink, and an OnsetVocabulary; make state private behind the few operations callers use (play_note, prepare_voice_at, continue_voice_live, finish_open_lines); replace fprintf with a diagnostics callback/sink.


## F144 [UNVERIFIED] medium (reporter: medium) workaround-hack — engine/include/mforce/render/instrument.h:181

**hiBoost hidden loudness law (backlog 19) still live; formula duplicated in two methods and the magic 0.3 hardcoded at six call sites**

Reporters: arch-hacks-census

Evidence: `instrument.h:181 `float hiBoost{0.0f};`; :359-361 `float boost = hiBoost > 0.0f
        ? (std::log10(std::max(freq, 100.0f)) - 2.0f) * hiBoost
        : 0.0f;` repeated verbatim at :565-567; tools/mforce_cli/main.cpp:240 `ip.instrument->hiBoost = 0.3f;` (also :409, :547, :699, :864; tools/test_figures/main.cpp:1162)`

Why: dsp/BACKLOG.md:134-140 names this 'a keytrack curve wearing a scalar's clothing' and schedules its conversion to a visible CurveNode. Meanwhile the law is applied in two code paths that can diverge (play_note vs prepare_voice_at already differ: :568 omits `* volume`), and five CLI harnesses carry an unexplained 0.3 that changes every render's level without appearing in any patch JSON — invisible to the null gate and to the UI.

Recommendation: Execute backlog 19: express the boost as a CurveNode from the Note face into the voice-gain path, delete `hiBoost`, and let the harnesses set it in the patch (or a shared constant) so the level law is data, not code.


## F145 [UNVERIFIED] high (reporter: high) rt-safety — engine/include/mforce/render/instrument.h:193

**Live-path API (prepare_voice_at / continue_voice_live / acquire_voice) allocates and mutates the live graph in place, so the only valid caller discipline is a mutex the audio callback also takes**

Reporters: render-instrument

Evidence: `"serialize these under their audio mutex — not internally synchronized" (193-194); `return { vg.source, durSamples + tailSamples, gain, vg.performSource, vg.advanceList };` (371-372, copies a vector<shared_ptr>); `auto g = std::make_shared<Envelope>(sampleRate); ... g->add_stage(...)` (422-426, reached from continue_voice_live 508→478); `if (int(slotInUse.size()) != n) slotInUse.assign(size_t(n), 0);` (201); `b.consumer->set_param(b.paramName, b.cs);` (342); `vg.topMultiplex->set_clone_param(...)` (344, builds `nodeId + "." + paramName` and map-inserts, multiplex_source.h 100-101)`

Why: Verified in mforce_ui/main.cpp: audio_callback holds `std::lock_guard<std::mutex> lock(g_audioMutex);` (3864) and the UI holds the same mutex around prepare_voice_at (4676, 4788) and continue_voice_live (4749, 4830). Every heap allocation, string-keyed set_param and map insert in these entry points therefore runs while the audio thread is blocked on the lock — a priority-inversion dropout source on every key-down/legato, and a direct violation of the no-locks/no-allocation render-path rule, forced by this unit's design rather than by the UI.

Recommendation: Make the live delivery allocation-free and double-buffered so the callback can drop the mutex: pre-allocate one glide Envelope and one bend Envelope per VoiceGraph at load and reset them in place (Envelope already has replace_stages/prepare); resolve PushBinding to a pre-bound slot (store the param's shared_ptr target or an index instead of a string name) at load; have StreamingVoice reference the VoiceGraph (pointer/span) instead of copying advanceList; size slotInUse at pool build. Then expose an SPSC command queue (note-on/legato/note-off) consumed at the top of the callback, which lets mforce_ui remove the lock from audio_callback.


## F146 [CONFIRMED] medium (reporter: high) soundness — engine/include/mforce/render/instrument.h:246

**StreamingVoice's per-sample protocol (tick → next → advance) is enforced by comment only and is already violated by mforce_cli --explore, which renders bend/loop patches wrong**

Reporters: render-instrument

Evidence: `"Streaming callers MUST call performSource->tick() once per sample before source->next()" (254-256); "Streaming callers MUST tick each entry once per sample AFTER source->next(), or tap-closed feedback loops fall silent" (259-261). Consumer tools/mforce_cli/explore.cpp 355-362: `auto sv = pitched->prepare_voice(...); ... mono[size_t(i)] = soft_clip(sv.source->next() * sv.gain);` — no tick(), no advanceList.`

Why: Traced all three streaming consumers: the UI audio callback (main.cpp 3889, 3899) and the UI keyboard CLI path (13287-13289) honor the contract; explore.cpp does not. By the header's own words, --explore renders of any patch with a tap-closed feedback loop (the flute/oboe loop family, the project's best sounds) are silent or wrong, and bend/wheel freeze at note-on. Explore is the batch sound-search pillar, so sweeps are being scored on incorrect audio. A contract that three callers must each reimplement per sample is a design defect, not a caller bug.

Recommendation: Give StreamingVoice a `float next()` that performs tick → source->next() * gain → advanceList ticks, make `source`, `performSource` and `advanceList` private, and switch all three consumers to it. Fix explore.cpp in the same change.

- verifier: Evidence verified: StreamingVoice contract comments at instrument.h 254-263; explore.cpp 355-362 calls prepare_voice then sv.source->next()*sv.gain with no performSource->tick() and no advanceList ticks; the UI callback (main.cpp 3889/3899) and keyboard CLI (13287-13289) do honor it; grep confirms exactly these three prepare_voice/prepare_voice_at consumers. Consequence is overstated on three counts. (1) 'bend/wheel freeze' is moot for explore: it passes no PitchCurve and never touches instrumentState, and PerformSource::frequency() returns the untouched base when bend_ is null (perform_source.h 84-86). (2) 'flute/oboe loop family silent or wrong' is FALSE: collect_advance_ids (patch_loader.cpp 184-221) only lists ids referenced by tap and NOT by ref/inputs; in oboe1.json Delay_line is tapped (169) but also ref-consumed (348, 372), so advanceList is empty — a scan mirroring collect_advance_ids over all 80 patches/library/ files finds 0 tap-only tails (4 patches use tap, none tap-only). (3) 'sweeps scored on incorrect audio' is unsupported: explore.cpp last changed 2026-07-03 (before tap edges shipped), renders/explore/ does not exist, and sweeps are generated by tools/gen_*.py and rendered via the CLI play_note/render_chunk path which ticks correctly. What does hold: 183 patches under sweep/+audition/ (bwg_perc BandedWG D0-D5, pierce1d Damp_lpf, string_harness NutDelay) and 4 baselines (double_advance_*, feedback/loop_*) carry tap-only tails and would mis-render through --explore. Real but latent contract violation in a currently dormant tool; medium, not high.
- verifier: Evidence verified: instrument.h 254-263 carries the two MUST comments; explore.cpp 355-362 calls prepare_voice then pulls sv.source->next() with no performSource->tick() and no advanceList tick; main.cpp 3889/3899 (audio callback) and 13287-13289 (keyboard CLI) do both. Consequence only partly follows. (a) Bend/wheel freeze is moot in --explore: perform_source.h:70-77 tick() only advances the bend envelope and wheel/pressure smoothers; explore passes no PitchCurve and has no MIDI, so the untouched clock changes nothing. (b) The named example is wrong: none of the library winds produce an advanceList entry — oboe1/oboe2/bassoon1's tapped Delay_line also has {"ref"} consumers (oboe1.json 348, 372) so collect_advance_ids (patch_loader.cpp 184-221) excludes it, and flute1's tapped 'delay' is the output node (flute1.json 242), also excluded. (c) But the defect is real for a large class: a JSON scan of patches/ with the collect_advance_ids rule finds ~170 instrument-style patches with tap-only tails (BandedWG D0..Dn in sweep/audition/baselines, STK bowed NeckDelay/TorsDelay, string_harness NutDelay, saxofony D0/D1, blowhole ThFilt, baselines/feedback loop_tap_starved); --explore renders of those never advance the loop tail. (d) '--explore is the batch sound-search pillar / sweeps are being scored wrong' is overstated: docs/autonomy references --explore once (2026-07-29); it is not the active sweep driver. Net: contract-by-comment violated by one of three consumers, with wrong audio for tap-only-tail patches under --explore.

## F147 [CONFIRMED] low (reporter: low) soundness — engine/include/mforce/render/instrument.h:268

**prepare_voice has no empty-pool guard while play_note does; a patch with "polyphony": 0 is a modulo-by-zero on the render-path loader**

Reporters: render-instrument

Evidence: ``int slot = int(nextVoice % int(voicePool.size()));` (268) vs `if (voicePool.empty()) return;` (522). patch_loader.cpp 1516 `int polyphony = instJson.value("polyphony", 4);` with no floor (the instrument-path loader at 1841 does apply `std::max(..., minPolyphony)`).`

Why: Undefined behavior (integer division by zero) reachable from explore.cpp 355 and the UI keyboard CLI 13284 with a degenerate but loadable patch.

Recommendation: Return an empty StreamingVoice (null source) when the pool is empty, and floor polyphony at 1 in both loaders.

- verifier: Verified: instrument.h 268 `nextVoice % int(voicePool.size())` with no empty guard; play_note 522 has `if (voicePool.empty()) return;`. patch_loader.cpp 1516 reads polyphony with no floor; 1841 applies std::max(.., minPolyphony) but patch_loader.h 28/34 default minPolyphony = 0, so the floor is a no-op by default. Reachability confirmed: explore.cpp 351 load_instrument_patch(path) and main.cpp 13280 load_instrument_patch_json(json) both use the default 0, then call prepare_voice (355 / 13284) -> modulo by zero on `"polyphony": 0`. The live UI path (main.cpp 4643) passes LIVE_MIN_POLYPHONY so it is guarded. Degenerate patch only; low.
- verifier: instrument.h:268 `nextVoice % int(voicePool.size())` with no empty guard; play_note at 522 guards. patch_loader.cpp:1516 `instJson.value("polyphony", 4)` has no floor; 1841 is `std::max(value, minPolyphony)` but patch_loader.h:28/34 default minPolyphony = 0, so it is not a floor at 1 either — "polyphony": 0 (or negative) builds an empty pool through both loaders. Reachable: explore.cpp:351-355 (load_instrument_patch default minPolyphony → prepare_voice) and main.cpp 13280-13284 (load_instrument_patch_json → prepare_voice). The UI live path is NOT reachable: acquire_voice returns -1 on n==0 and main.cpp 4678/4790 return on slot<0 before prepare_voice_at. Degenerate hand-edited patch only.

## F148 [CONFIRMED] medium (reporter: medium) smell — engine/include/mforce/render/instrument.h:306

**Library code writes diagnostics straight to stderr/cerr (58 sites in engine/)**

Reporters: arch-modern-cpp

Evidence: `306 `std::fprintf(stderr, "[onset] name '%s' is not in this "`, also 553, 651, 700; patch_loader.cpp 12 fprintf(stderr) sites; composer.h:250, 260, 637, 654, 1586, 1631, 1633, 2041 `std::cerr << ...``

Why: mforce_ui is a WIN32-subsystem app where stderr is swallowed (its own crash-log comment at 12781-12783 says so), so engine warnings about unknown onsets, dropped bindings and fallback strategies are invisible exactly where patches are edited; fprintf is also not RT-safe (instrument.h:553/651/700 sit in note-on paths).

Recommendation: Route through a `mforce::log(Level, string_view)` sink with a settable callback (default: stderr in CLI, status bar/crash log in UI), and make RT-path warnings set a flag that is reported off-thread.

- verifier: Evidence verified: instrument.h 306, 553, 651, 700 are fprintf(stderr); composer.h 250, 260, 637, 654, 1586, 1631, 1633, 2041 are std::cerr; patch_loader.cpp has 12 fprintf(stderr) sites; engine/ totals 56 fprintf(stderr) + 73 std::cerr (finding's '58' is roughly the fprintf count). Consequence verified: main.cpp 12781-12783 states the GUI subsystem swallows stderr, 12958-12967 only AttachConsole/freopen stderr when argv[1] starts with '-' (CLI subcommands), and 4001/4277 comments already acknowledge fprintf(stderr) is invisible in the GUI. One sub-claim refuted: 553/651/700 are not RT paths — 553 is in play_note (offline Generate/CLI only; the live path uses prepare_voice_at/apply_note_bindings, which have no fprintf) and 651/700 run after the render loop; onset_id (306) is called from the UI thread at schedule time (4409, 4762, 4845), never from the audio callback. Smell with a concrete cost (engine warnings invisible where patches are edited); medium.
- verifier: instrument.h 306, 553, 651, 700 are std::fprintf(stderr); patch_loader.cpp has 12 fprintf(stderr) sites; composer.h 250, 260, 637, 654, 1586, 1631, 1633, 2041 are std::cerr — all confirmed. The '58 sites' figure is an undercount: engine/ has 56 fprintf(stderr) + 73 std::cerr = 129. tools/mforce_ui/CMakeLists.txt:41 sets WIN32_EXECUTABLE TRUE; main.cpp 12958-12967 attaches the parent console only when argv[1] starts with '-', so GUI launches never see stderr; main.cpp 4001-4003 and 4277 comments independently say so. One sub-claim is overstated: 553/651/700 are in play_note/finish_line, which run on the UI thread (generate_unified, callers at 8343/8393) or CLI — never the RtAudio callback (3857-3899 pulls voice.source->next() directly), so 'not RT-safe' does not apply to any audio-thread path. Invisible diagnostics in the UI is a concrete cost; a log sink is a legitimate fix, not a CLAUDE.md conflict.

## F149 [REFUTED] low (reporter: high) rt-safety — engine/include/mforce/render/instrument.h:442

**render_chunk resizes the output vector per call and is called with n=1 per sample; its 'caller must reserve' contract is only honoured for the first note of a line**

Reporters: arch-modern-cpp

Evidence: `442: `buf.resize(base + size_t(n));`; 631 and 686: `render_chunk(vg, buf, vIdx, startFrame, 1, gain);` / `render_chunk(vg, buf, line_.vIdx, startFrame, 1, line_.gain);`; contract at 436-437 "buf must be reserved by the caller: the resize below then allocates nothing" but the only reserve is 590-591 `line_.buf.reserve(size_t(durSamples + int(kMaxRingSec * float(sampleRate))));` sized for the first note's duration`

Why: A held line whose later notes extend past the first note's reserved capacity reallocates line_.buf (copying the whole line) in the middle of per-sample rendering, on whichever thread drives the instrument (the UI's live path).

Recommendation: Reserve for the line's maximum (or grow in prepare/at note-on outside the per-sample loop), or give render_chunk a span/pointer target with the caller responsible for capacity, and assert `buf.capacity() >= base + n` in debug.

- verifier: Evidence lines are accurate: 442 resize per call, 631/686 n=1 per sample, 436-437 'caller must reserve', 590-591 reserve = first note durSamples + kMaxRingSec(8 s, line 118)*sr. A continuation line whose total exceeds that does reallocate inside render_chunk. But the stated consequence does not follow: render_chunk is only reached via play_note/finish_line (grep: 534, 592, 631, 686), and play_note is called only from the UI's offline Generate (main.cpp 4408, inside generate_unified), patch_loader's score loop, Conductor and engine_tests — never from the audio callback. The UI's live path (4691, 4801) uses prepare_voice_at and the callback pulls source->next() directly (3889-3899). finish_line itself calls buf.reserve(maxSamples) at 676, i.e. the design already accepts reallocation at line boundaries on the offline path, and the classic path allocates a fresh std::vector per note (623). So this is an offline-only amortized realloc for lines longer than ~8 s past the first note, not an RT-safety hazard; 'rt-safety/high' is wrong. Low.
- verifier: Code facts partly correct: 442 resizes, 631 and 686 call render_chunk with n=1. But 'the only reserve is 590-591' is false — the classic path reserves at 623-624 (max(renderSamples, maxSamples)) and finish_line reserves at 676 (maxSamples) BEFORE their n=1 loops, so those per-sample calls allocate nothing. The only path that can exceed capacity is the continuation branch (534-536), which calls render_chunk ONCE per note with n=durSamples: the resize at 442 happens before the loop, so a reallocation is once-per-note, not 'in the middle of per-sample rendering'. The thread claim is wrong: play_note/render_chunk are offline only — UI callers are generate_unified (4347, called from UI-thread buttons at 8343/8393 and CLI subcommands 13075/13296); the live keyboard uses prepare_voice_at (4691/4801) and the audio callback (3857-3899) pulls voice.source->next() itself and never enters render_chunk. Per-note allocation on the offline path is the existing design (classic path allocates a fresh std::vector per note at 623, add_rendered copies at 90). No RT-safety consequence follows.

## F150 [CONFIRMED] low (reporter: low) soundness — engine/include/mforce/render/instrument.h:475

**Continuation reseed and offline capture both depend on nodesById, which the render-path loader never fills — determinism and capture silently no-op for mixer-path instruments**

Reporters: render-instrument

Evidence: ``for (auto& [nid, src] : vg.nodesById) src->reseed();` (475) and `voicePool[v].nodesById.find(ids[k])` (235); the field is documented "Empty for mixer-path instruments." (163). patch_loader.cpp assigns `vg.nodesById = std::move(g.valueNodes);` only at 1886 (instrument-path loader); the render-path pool build at 1534-1579 never sets it.`

Why: A render-path patch with a hold line gets the glide, set_note and push bindings but not the per-note draw re-anchoring the comment calls out as required ('a different oboeist per phrase'), with no diagnostic. The capture_begin path likewise captures nothing for such instruments.

Recommendation: Fill nodesById in the render-path loader too (it already has g.valueNodes in scope), or make deliver_continuation reseed via vg.source->reseed() recursively so determinism does not depend on a side table.

- verifier: Evidence verified: instrument.h 475 iterates vg.nodesById for reseed; 235 capture_begin resolves ids through nodesById; 163 comment says 'Empty for mixer-path instruments.' Grep of patch_loader.cpp: nodesById assigned only at 1886 (load_instrument_patch_json); the render-path pool build (1534-1579) never sets it, though it does fill allEnvelopes (1553) and sustaining (1528), so a render-path patch with hold:true (e.g. patches/baselines/phrase_smoke_phrased.json via the embedded score at 1630) opens a line and deliver_continuation skips the per-note reseed silently. Capture sub-claim is true but not exercised: capture_begin's only caller is main.cpp 4398, whose instrument comes from load_instrument_patch_json (4352), so capture never runs on a render-path instrument today. All engine_tests determinism tests (1091-1157) use the instrument-path loader, so the gap is untested rather than observed. Low, as the finding itself rates it.
- verifier: Grep confirms nodesById is assigned only at patch_loader.cpp:1886 (instrument-path loader); the render-path pool build (1534-1579) never sets it, and the field comment at 163 admits it. deliver_continuation (475) therefore reseeds nothing for render-path instruments; reseed() is overridden by 9 stochastic sources (envelope, vibrato, white/pink/red/layered-red/wander noise, segment, noise_sources), and the per-note-determinism test (engine_tests/main.cpp:1153) uses load_instrument_patch_json, so it never exercises the gap. The render-path loader DOES deliver hold lines (1609-1631 → play_note with hold) and mforce_cli's default render uses load_patch_file (main.cpp:769), so CLI renders of a hold-line score lose per-note draw anchoring silently. Reach today is small: Conductor never sets hold (all conductor.h play_note calls pass false/default) and only one score in patches/ uses hold:true (baselines/phrase_smoke_phrased.json). The capture_begin sub-claim is code-true but unreachable: its only caller is UI generate_unified (4352/4398), which loads via load_instrument_patch_json and so always has nodesById.

## F151 [CONFIRMED] low (reporter: medium) soundness — engine/include/mforce/render/instrument.h:527

**Continuation branch silently ignores the note's startTime and its PitchCurve**

Reporters: render-instrument

Evidence: ``if (line_.open) { auto& vg = voicePool[size_t(line_.vIdx)]; deliver_continuation(vg, freq, pn.velocity, durSamples, pn.duration, line_.lastFreq, pn.onsetId); ... render_chunk(vg, line_.buf, line_.vIdx, int(line_.startTime * float(sampleRate)), durSamples, line_.gain); if (!pn.hold) finish_line(); return; }` — `startTime` and `pn.curve` are never referenced; `deliver_continuation(VoiceGraph&, float freq, float velocity, int durSamples, float durSeconds, float fromFreq, float onsetId)` (472-474) has no curve parameter.`

Why: The score loader passes an explicit `time` when present (patch_loader.cpp 1607-1608 `noteJson.contains("time") ? noteJson["time"].get<float>() : prevEnd`), so a hold:true note followed by a note with a later `time` has its rest swallowed and every subsequent onset in the line lands early with no warning. A PitchCurve on a continued note is dropped (latent today — conductor.h never sets hold — but PerformedNote carries both fields, so nothing stops a future Performer from doing it).

Recommendation: In the continuation branch compare `startTime` to `line_.startTime + buf.size()/sampleRate`; if it deviates by more than one sample either close the line first (finish_line then fresh note) or warn. Either reject `pn.curve` on a continued note with a stderr/diagnostic, or compose it with the glide (PerformSource holds a single `bend_` slot, so composition needs a design decision).

- verifier: Evidence verified: instrument.h 527-539 continuation branch uses line_.startTime and never reads startTime or pn.curve; deliver_continuation signature (472-474) has no curve parameter; PerformedNote (33-40) carries both hold and curve. patch_loader.cpp 1607-1608 takes an explicit 'time' when present and 1624-1635 passes it to play_note without checking it against prevEnd on an in-line note, so a gap after a hold:true note is swallowed with no diagnostic; main.cpp gencheck 13065-13070 mirrors the same reading. Scope is narrower than the title implies: Conductor never sets hold (conductor.h has no hold usage; curve-bearing calls at 212/239 pass hold=false), and the UI passage path (8371-8383) ends a phrase at every rest, so held notes it emits are always contiguous. Reachable today only from hand-written embedded scores; the curve-on-continuation drop is latent. Silent discard of an explicit score field with no warn is worth a one-line check; medium.
- verifier: instrument.h 527-539: the continuation branch reads line_.startTime, line_.vIdx, line_.lastFreq, line_.gain and never pn.curve or the startTime argument; deliver_continuation (472-474) has no curve parameter; render_chunk appends at line_.startTime + buf.size(), so a continued note always lands contiguously. patch_loader.cpp 1607-1608 does pass an explicit "time" when present and 1630-1631 forwards it, with no check that it equals prevEnd. So a hold:true note followed by a note with a later time has its rest swallowed and all later onsets shift early, silently — confirmed. The spec (2026-09-20-note-onsets-v2-design.md:115) says 'a rest ends the phrase; the note before a rest is hold:false', placing the rule on the parser/Performer, but the score-block loader enforces nothing. Latency confirmed: conductor.h never sets hold (every play_note call passes false/default) and the sole hold:true score (baselines/phrase_smoke_phrased.json) has no gap. No current producer hits it; cost today is nil, a one-line startTime check/warn would close it.

## F152 [CONFIRMED] medium (reporter: medium) soundness — engine/include/mforce/render/instrument.h:609

**fire_triggers after prepare on the fresh offline path re-anchors stage 0 at the slot's stale cur_, and the live path never fires triggers at all — fresh notes diverge offline vs live**

Reporters: render-instrument

Evidence: `Fresh paths: `vg.source->prepare(ctx, durSamples); ... fire_triggers(vg);` (578-580 and 607-609). Live path prepare_voice_at (349-373) calls apply_note_bindings + prepare and never fire_triggers. envelope.h: prepare() resets `gateActive_ = false; ptr_ = -1; currStage_ = 0; stageStart_ = 0;` (362-366) but never cur_; retrigger() does `gateFrom_ = cur_; gateActive_ = true; gateStage_ = 0;` (147-149) and next_raw_ then evaluates `cur_ = gateFrom_ + (r.endVal - gateFrom_) * shape;` (433) instead of the ramp's own startVal.`

Why: Traced: after any completed offline note the envelope sits at cur_ = 0 (envelope.h 418-421), so a triggered envelope whose stage 0 starts at a nonzero value (a pitch-offset or 'from 1 decay' onset gesture) begins its fresh note at 0 rather than its authored start — and only offline. The header's design claim at 517-520 is that offline and live are "one mechanism"; here they are not. On the live path a trigger-wired envelope never fires on a fresh key-down.

Recommendation: Decide the fresh-note semantics once: either skip fire_triggers on fresh paths (prepare already places the envelope at stage 0 with its authored startVal) or have retrigger take an explicit anchor and pass the stage-0 startVal on fresh notes. Then route prepare_voice_at through the same post-prepare sequence as play_note so live and offline fresh notes are byte-identical, and add an engine_tests case for a trigger envelope with nonzero startVal on both paths.

- verifier: Evidence confirmed: fire_triggers after prepare at instrument.h 578-580 (held fresh) and 607-609 (classic fresh); prepare_voice_at 349-373 calls apply_note_bindings (onsetId defaults to 0) + prepare and never fire_triggers, and no caller in tools/ calls it (grep: fire_triggers only in instrument.h). Envelope is engine/include/mforce/core/envelope.h (finding omits the dir): prepare() 295-368 resets gateActive_/ptr_/currStage_/stageStart_ but never cur_ (declared 534, init 0, only written in next_raw_); retrigger() 145-153 does gateFrom_ = cur_; next_raw_ 426-434 re-anchors stage 0 as gateFrom_ + (endVal - gateFrom_) * shape. So on a fresh note cur_ is the previous note's last output (0 after a completed note, 418-421) or the initial 0, and a nonzero authored startVal is replaced. Reachable offline: score loader passes onset names on fresh notes (patch_loader.cpp 1624-1631) and the UI Generate path does too (main.cpp 4408-4410). Spec docs/superpowers/specs/2026-09-20-note-onsets-v2-design.md section 4 step 1 says a fresh voice fires onset triggers, so the live path omitting them contradicts the spec and the two paths diverge. Caveats: no patch under patches/ wires "trigger" today (grep), engine_tests run_onset_trigger_tests only exercises the continuation case; the 517-520 comment cited is about continuation detection, not fresh-note triggers (loose cite, immaterial).
- verifier: Evidence verified: fire_triggers follows prepare on both fresh paths (instrument.h 578-580, 607-609); prepare_voice_at (349-373) calls apply_note_bindings with the DEFAULT onsetId=0 and never fire_triggers. envelope.h matches the quotes: retrigger() 145-153 sets gateFrom_=cur_ and gateStage_=0; prepare() 295-368 resets ptr_/currStage_/stageStart_/gateActive_ but never cur_ (cur_{0.0f} at 534); next_raw_ 418-421 leaves cur_=0 past the last stage; 426-434 interpolates gateFrom_ -> endVal on the gate stage. Traced the first sample after prepare+retrigger: pos=0 gives shape=0, so cur_=gateFrom_ (stale 0) and a stage authored 1.0->0.1 renders as 0->0.1 — the fresh gesture is flattened, offline only. Two corrections: (1) the title's 'the live path never fires triggers at all' is false — continue_voice_live -> deliver_continuation fires them (495); only live FRESH key-downs do not, and they also pass no onsetId, so a NameGate trigger would read 0 anyway. (2) Reachability is narrow today: both stamp rules (patch_loader.cpp 1602-1635 via gen scripts' 'breath if start', mforce_ui stamp_passage 4336-4344 'firstOfPhrase ? breath') give fresh notes 'breath', and every on-disk NameGate (4 audition patches) matches 'tongue', so the fresh+nonzero-trigger path needs a non-sustaining patch receiving a tongued score note (1624-1631 passes onset through with hold forced false) or a future 'breath'-wired trigger. Spec §4 (2026-09-20-note-onsets-v2-design.md 133-136) does say fresh voices 'fire onset triggers', so the call is per spec; the defect is retrigger anchoring a just-prepared envelope at stale cur_. run_onset_trigger_tests (engine_tests 1063-1126) covers continuation only. Latent correctness flaw with a one-line fix; medium.

## F153 [CONFIRMED] medium (reporter: high) duplication — engine/include/mforce/render/instrument.h:620

**Ring-out / cap-fade / containment block is copy-pasted between play_note (classic) and finish_line, and mirrored a third time in the UI audio callback**

Reporters: arch-build-headers, arch-duplication, render-instrument

Evidence: `play_note 620-655: `const float ringDecay = std::exp(-1.0f / (0.05f * float(sampleRate)));` ... `if (i >= renderSamples && ringEnv < kRingFloor) break; render_chunk(vg, buf, vIdx, startFrame, 1, gain); ringEnv = std::max(std::fabs(buf[size_t(i)]), ringEnv * ringDecay);` ... `[containment] note %.1f`; finish_line 673-704 repeats the same ringDecay/loop/fadeN/containment nearly verbatim (`[containment] line ending on note`). Boost/gain formula also duplicated: 359-362 vs 565-568 `(std::log10(std::max(freq, 100.0f)) - 2.0f) * hiBoost`. mforce_ui/main.cpp 3900-3917 is a third hand-copy ("mirrors PitchedInstrument::play_note") with ringDecay baked as 0.99958 for 48 kHz.`

Why: This policy has already changed three times by the comments' own record (backlog 63, 63b, REVIEW 09-16); each change had to be applied in three places and the UI copy has already drifted (hard-coded sample rate, 100 ms chunking). The next tweak will desynchronize offline vs live ring-out audibly.

Recommendation: Extract `int ring_out(VoiceGraph&, std::vector<float>& buf, int vIdx, int startFrame, int base, int releaseRem, float gain)` returning the rendered count, and `void containment_check(const float*, int rendered, ...)`; call both from play_note and finish_line. Extract `float voice_gain(float freq, float velocity) const` for the boost formula. Expose a small incremental RingFollower struct so the UI callback can reuse the same constants instead of a hand-mirror.

Also reported as: Ring-out / cap-fade / containment-report loop duplicated between play_note and finish_line | Ring-out / cap-fade / containment loop copied between play_note and finish_line, and a third hand copy in the UI audio callback

- verifier: Both engine copies exist and are materially identical: ring loop play_note 627-640 vs finish_line 677-692 (same ringDecay, kRingFloor break, render_chunk n=1, fadeN=0.08 s cap fade), containment check 646-653 vs 695-704, boost formula 359-362 vs 565-568. Third copy confirmed in tools/mforce_ui/main.cpp 3730 (comment literally says 'mirrors PitchedInstrument::play_note') and 3900-3917 with 0.99958f, 0.001f, 4800-sample chunks, last-chunk fade; ringBudget = kMaxRingSec*48000 at 3805-3806. Policy history (backlog 63, 63b, REVIEW 09-16) confirmed in comments at 101-118, 614-619, 634-636. Drift is real but narrower than stated: the 48 kHz bake is a documented decision (backlog 69, comment 3901-3902), not accidental drift; the real behavioral divergence is 100 ms extension granularity and a 100 ms last-chunk fade vs the engine's per-sample decision and 80 ms fade. Severity medium rather than high: no correctness failure today, divergence only touches notes that reach a chunk boundary or the ring cap.
- verifier: Both engine copies verified and materially identical: ring loop 629-640 vs 684-692, ringDecay 627 vs 677, fadeN 637 vs 689, containment 646-653 vs 695-704 (only the message text and the index-cast differ). Boost formula duplicated at 359-362 vs 565-568 (note the gain line itself differs on purpose: prepare_voice_at multiplies by volume because streaming callers bypass Instrument::render). UI mirror verified at main.cpp 3900-3917: 0.99958f (= exp(-1/(0.05*48000)), comment says the 48 kHz bake is deliberate per backlog 69), 0.001f, 4800-sample chunks, and a fade over the final 4800-sample chunk (100 ms) vs the engine's 80 ms fadeN — the drift claim is true, but it only manifests at the 8 s kMaxRingSec cap for near-lossless resonators. The three-change history is supported by the comments (backlog 63, 63b, REVIEW 09-16). Severity: the engine pair is a clean extraction target with a real maintenance cost, but the UI copy is a structurally different incremental/RT formulation that cannot share the offline loop verbatim, and the only existing audible divergence is a 20 ms cap-fade difference at the 8 s ceiling — 'high' overstates it. Same claim as F155.

## F154 [CONFIRMED] low (reporter: medium) efficiency — engine/include/mforce/render/instrument.h:624

**Per-note 8-second transient reserve plus a second full copy in add_rendered, and a per-sample render_chunk call that resizes on every sample**

Reporters: render-instrument

Evidence: ``buf.reserve(size_t(std::max(renderSamples, maxSamples)));` (624) where `maxSamples = durSamples + int(kMaxRingSec * float(sampleRate))` (622); `render_chunk(vg, buf, vIdx, startFrame, 1, gain);` (631, and 686) with `buf.resize(base + size_t(n));` inside (442); `rn.samples.assign(data, data + count);` (90) copies the whole buffer again; DrumKit does the same (741 `std::vector<float> buf(durSamples);` → 745 add_rendered). Line path: 590-591 reserves durSamples + 8 s; 676 re-reserves.`

Why: At 48 kHz every note allocates and frees ~1.5 MB+ of capacity it almost never uses, then memcpys the rendered portion into a second vector — for a 1000-note piece that is ~1.5 GB of allocator churn and a full extra pass over all audio. The main offline sample loop pays a function call, a resize, a capture branch and an advanceList range-for setup per sample instead of per note. Not real-time, but this is the whole-piece render path the comp lane waits on.

Recommendation: `add_rendered(float startTime, std::vector<float>&& samples)` to move instead of copy; reserve `renderSamples` and let the ring extension grow the vector only when it actually extends; have ring_out call render_chunk with n = the chunk size and hoist the capture/advanceList checks out of the k-loop (or have render_chunk write into a pre-sized span).

- verifier: Evidence confirmed: reserve(max(renderSamples, maxSamples)) at 624 with maxSamples = durSamples + 8 s (622); render_chunk called with n=1 at 631 and 686, buf.resize(base+n) at 442; add_rendered copies via samples.assign at 90; DrumKit 741/745; line path reserves durSamples+8 s at 590-591 and re-reserves at 676. Arithmetic (1.5 MB/note at 48 kHz) is right. But the 'why' overstates cost: the reserve is one large-block allocation per note whose untouched capacity is never written or copied, and the assign copy is a rendered-length memcpy; both are well under 1% of a note's per-sample DSP cost, and this is the offline path, not RT. The claim that the loop pays 'a capture branch and an advanceList range-for setup per sample instead of per note' is wrong: both are inside render_chunk's k-loop regardless of n and advanceList must tick per sample by contract (446, 155-160), so the hoisting in the recommendation is not available. Upfront reserve is the stated mechanism that keeps the loop allocation-free (437); moving instead of copying in add_rendered is the one clean win. Low severity: no measured cost, no rule violated.
- verifier: Facts verified: 622/624 reserve durSamples + 8 s (kMaxRingSec 118) per note; render_chunk called with n=1 at 631 and 686, resize at 442 per call; add_rendered copies via assign at 90; DrumKit 741 -> 745; line path 590-591 reserve and 676 re-reserve. The mechanics follow (per-note large reserve, one extra copy of rendered audio, per-sample call/resize/branch/range-for overhead). The magnitude is overstated: '~1.5 GB of allocator churn' is nominal capacity — a 1.5 MB reserve on Windows is a VirtualAlloc/VirtualFree pair with pages faulted only where written, so touched memory equals the rendered portion either way; the memcpy of rendered audio and the per-sample bookkeeping are small next to a full graph pull per sample; nothing is measured. The per-sample render_chunk(…,1,…) is explicitly designed ('buf must be reserved by the caller: the resize below then allocates nothing', 436-437) and the ring-out decision is inherently per-sample. Not a hot/RT path. Moving instead of copying in add_rendered is a trivial win; the rest is unmeasured efficiency, so low.

## F155 [CONFIRMED] medium (reporter: medium) duplication — engine/include/mforce/render/instrument.h:629

**Ring-out + cap-fade + containment loop is triplicated with diverging constants (play_note, finish_line, UI audio callback)**

Reporters: arch-render-pipeline

Evidence: `instrument.h:629-640 (play_note) and 684-692 (finish_line) are the same loop verbatim: `if (i >= renderSamples && ringEnv < kRingFloor) break; render_chunk(...,1,...); ringEnv = std::max(std::fabs(buf[i]), ringEnv * ringDecay); ... if (i >= maxSamples - fadeN && ringEnv >= kRingFloor) buf[i] *= ...`, each followed by the identical containment check (646-653 vs 695-704). main.cpp:3903-3917 is a third implementation with hardcoded `0.99958f`, `0.001f`, `4800` chunks and a 100 ms last-chunk fade instead of the engine's kRingFloor / 50 ms ringDecay / 80 ms fadeN.`

Why: Three copies of the behaviour that defines how every note ends means a fix lands in one and not the others (the UI already fades over a different window than the engine, so live and Generate end notes differently). The engine copies also call render_chunk with n=1 per sample, paying a vector::resize and the `capturing` test per sample.

Recommendation: Factor a single `RingOut` helper in the engine (follower state + decision + fade gain per sample, constants in one place) used by play_note, finish_line and, in block form, the UI callback; have play_note/finish_line share one `render_tail(vg, buf, base, rem)` function.

- verifier: Duplicate of F153. Loops at 629-640 and 684-692 and containment at 646-653 / 695-704 confirmed verbatim; main.cpp 3903-3917 confirmed as the third implementation with 0.99958f, 0.001f, 4800 chunks, last-chunk fade. 'Diverging constants' is partly overstated: 0.001f equals kRingFloor and 0.99958 equals exp(-1/(0.05*48000)) = 0.999583 at the pinned 48 kHz live rate, so the floor and follower match; the real divergence is the 80 ms engine fade vs the UI's 100 ms last-chunk fade and 100 ms extension granularity, which does make live and Generate end ringing notes differently. Per-sample render_chunk(n=1) resize + capturing test confirmed (440-442). Medium: maintenance cost and a real but narrow behavioral mismatch.
- verifier: Duplicate of F153 — same claim, same verification. instrument.h 629-640 and 684-692 verbatim-equivalent loops, containment 646-653 vs 695-704 verified. main.cpp 3903-3917 verified: 0.99958f, 0.001f, min(4800, ringBudget) chunks, fade spread over the last chunk (100 ms) vs engine fadeN 80 ms. Correction on 'diverging constants': kRingFloor (0.001) and the 50 ms follower (0.99958 at 48 kHz) match the engine numerically; only the cap-fade window and the baked sample rate differ, and the fade divergence is audible only when a voice reaches the 8 s cap. The 'render_chunk with n=1 pays resize + capturing test per sample' remark is accurate (442, 447). Medium, not high, for the reasons in F153.

## F156 [CONFIRMED] low (reporter: low) smell — engine/include/mforce/render/instrument.h:651

**Engine render path writes diagnostics straight to stderr, which the WIN32 UI build cannot display**

Reporters: render-instrument

Evidence: ``std::fprintf(stderr, "[containment] note %.1f ..."` (651-653), `"[containment] line ending on note"` (700-704), `"[onset] name '%s' is not in this instrument's onsets[] vocabulary"` (306-308), `"[onset] hold requested but this voice has no reachable envelopes"` (553-555). mforce_ui/main.cpp 4001-4003: "fprintf(stderr) is invisible in a WIN32_EXECUTABLE build".`

Why: The containment check is the engine's one objective 'this note clicks' detector and the UI user never sees it; the engine has no diagnostics sink the tools can route to a status line or log.

Recommendation: Add a minimal engine diagnostics hook (`void (*g_diag)(std::string_view)` or a std::function set by the tool) with a default to stderr; route these four sites through it.

- verifier: All four fprintf(stderr) sites confirmed at instrument.h 306-308, 553-555, 651-653, 700-704; tools/mforce_ui/CMakeLists.txt:41 sets WIN32_EXECUTABLE TRUE; main.cpp 4001-4003 comment confirmed. The UI's Generate goes through play_note (main.cpp 4408-4412) so containment reports are emitted there. main.cpp 12958-12967 attaches the parent console and reopens stderr only when argv[1] starts with '-' (headless modes), so in the normal GUI launch nothing reaches the user; no engine diagnostics hook exists (grep g_diag/log_sink/set_logger across engine/ empty) and main.cpp never reads 'containment'. Minor overstatement: headless modes launched from a console do see stderr. Low severity as the finding itself says.
- verifier: All four engine sites verified at the quoted lines (306-308, 553-555, 651-653, 700-704); patch_loader.cpp 1617 '[score] hold ignored' is a fifth in the same family. mforce_ui is a WIN32 subsystem app (tools/mforce_ui/CMakeLists.txt:41 WIN32_EXECUTABLE TRUE); main.cpp 12958-12967 attaches the parent console and reopens stderr ONLY for headless '-' modes, and the only other sink is the crash log (12786), so in a normal GUI launch Generate's [containment] warnings go nowhere — the consequence holds, and the UI itself records the same failure mode at 4274-4278 (status line was adopted precisely because stderr was missed). No engine-side diagnostics hook exists (grep across engine/include and engine/src found none). CLI renders still show the warnings, so the cost is confined to in-UI Generate; low as the finding says.

## F157 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/render/limiter.h:23

**soft_clip's HARD clamp is unreachable, the mixer comment states the wrong ceiling, and the file shares its name with an unrelated filter/limiter.h**

Reporters: filters

Evidence: `render/limiter.h:19-24: `float room = CEIL - THR; float shaped = THR + room * std::tanh(over / room); float y = sign * shaped; if (y > HARD) y = HARD; if (y < -HARD) y = -HARD;` with THR=0.95, CEIL=0.99, HARD=0.999; mixer.cpp:49 `// Peak guard: bound mix output to ±0.999.``

Why: tanh < 1 so |shaped| < THR + room = CEIL = 0.99 < HARD; the 'belt-and-suspenders' lines can never fire (NaN also passes through untouched since the comparisons are false). The mixer comment documents 0.999 while the real bound is 0.99. Relationship of the two limiter.h files is clear once read (filter/limiter.h:16-18 explains it: configurable lookahead graph node vs. fixed free-function safety clip used by mixer.cpp:51 and instrument.h:82), but two headers named limiter.h with unrelated contents is a navigation trap in a header-heavy codebase.

Recommendation: Rename render/limiter.h to soft_clip.h (one include line in mixer.cpp, instrument.h, mforce_ui/main.cpp, explore.cpp); delete the dead HARD clamp or make it meaningful (e.g. CEIL = HARD); fix the mixer.cpp:49 comment to ±0.99.


## F158 [UNVERIFIED] low (reporter: low) efficiency — engine/include/mforce/render/perform_source.h:84

**PerformSource::frequency() recomputes exp2 on every read; every PerformOut consumer pays it per sample**

Reporters: arch-render-pipeline

Evidence: ``float frequency() const { return bend_ ? note_.frequency * std::exp2(bendSemis_ / 12.0f) : note_.frequency; }` (84-87); PerformOut::next() and current() both call it (120-121, 128), and the file's own comment notes one PerformNode normally feeds many pins (117-119).`

Why: Under a bend or glide, N frequency consumers in a voice do N exp2 calls per sample instead of one, even though the value only changes in tick().

Recommendation: Compute `articulatedHz_` once in tick() (and in set_note) and have frequency() return the cached value.


## F159 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/additive/additive_source2.h:116

**AdditiveSource2's 'partials' pin snapshots the raw Partials arrays, bypassing the lazy arrayUpdateReq_ rebuild, so it receives constructor defaults instead of the patch's settings**

Reporters: additive

Evidence: `void apply_partials(const Partials& p) { int n = p.partial_count(); ... startIdx_  = p.get_mult1(); endIdx_ = p.get_mult2(); startAmpl_ = p.get_ampl1(); endAmpl_ = p.get_ampl2();`

Why: get_mult1()/get_mult2()/get_ampl1()/get_ampl2() (partials.h:404-407) return the members directly. Settings applied after construction only set arrayUpdateReq_ (e.g. FullPartials::set_setting, partials.h:1100-1107); the rebuild happens only in get_array() (858-859) or partials_prepare() (428-430). At load, wire_params_generic (patch_loader.cpp:895) calls set_param("partials") before any prepare, so a FullPartials configured with maxPartials 60 hands AdditiveSource2 the 30-partial ctor default. The getters exist solely for this caller (comment at 403).

Recommendation: Have apply_partials go through get_array("mult1") etc. (which rebuilds), or rebuild eagerly in set_setting; if AdditiveSource2 is deleted per the redundancy finding, remove the four getters with it.


## F160 [UNVERIFIED] high (reporter: high) duplication — engine/include/mforce/source/additive/basic_additive_source.h:16

**Three additive sources coexist; BasicAdditiveSource is fully subsumed by AdditiveSource+FullPartials and AdditiveSource2 is a half-integrated, buggy port that one baseline patch uses**

Reporters: additive

Evidence: `struct BasicAdditiveSource final : WaveSource {   (basic_additive_source.h:16)  //  struct AdditiveSource2 final : WaveSource {   (additive_source2.h:23)  //  struct FullAdditiveSource final : WaveSource {   (full_additive_source.h:22)  //  menu_source("Basic", "BasicAdditiveSource"); menu_source("Alternate", "AdditiveSource2");   (tools/mforce_ui/main.cpp:10835-10836)`

Why: BasicAdditiveSource's whole feature set (evenWeight/oddWeight/rolloff, per-partial freq/ampl wander) maps 1:1 onto FullPartials evenWeight1/oddWeight1/rolloff1 plus the motion/shimmer layers; no patch in patches/ references it. AdditiveSource2's only distinct capability is envelope-assignment by partial filter (assign_*_envelope), used by a single baseline patch, and the implementation carries the stale-pitch and unbounded-phase bugs above. The per-partial triangle-wave random walk is implemented three times (basic_additive_source.h:148-160, additive_source2.cpp:148-164, partials.h walk_advance 965-977). Both legacy types stay registered (source_registrations.cpp:84, 468) and in the UI node menu, so users can pick sources that sound wrong.

Recommendation: Delete BasicAdditiveSource (registry entry, UI menu item, header). For AdditiveSource2, either fix the two bugs and finish its Partials integration, or — preferable — port 'amplitude envelope by partial filter/range' into Partials as a small table over the existing amplEnv path and delete AdditiveSource2, migrating as2_bright_attack_test.json.


## F161 [UNVERIFIED] high (reporter: high) rt-safety — engine/include/mforce/source/additive/basic_additive_source.h:118

**BasicAdditiveSource resizes five std::vectors inside compute_wave_value (per-sample render path)**

Reporters: additive

Evidence: `if (count > partialCount_) { ... partialPos_.resize(count, 0.0f); freqOffset_.resize(count, 0.0f); amplOffset_.resize(count, 0.0f); freqVarDir_.resize(count, 0); amplVarDir_.resize(count, 0);`

Why: Direct violation of the no-heap-in-hot-loop rule. prepare() clears the vectors (lines 87-92), so the first sample of every note takes the grow path, and any downward frequency move mid-note (vibrato, bend) that raises count = 12000/currFreq_ (line 114) reallocates again. In the live UI the source's next() runs inside the RtAudio callback (tools/mforce_ui/main.cpp:3890), so this is an allocation on the audio thread.

Recommendation: Size the arrays once in prepare to a fixed maximum (12000 / minimum supported frequency, or a hard cap such as 512) and clamp count to that size in the loop; seed the random-walk state for all slots at prepare.


## F162 [UNVERIFIED] low (reporter: low) modern-cpp — engine/include/mforce/source/additive/formant.h:320

**RTTI cross-casts in the wiring path**

Reporters: arch-modern-cpp

Evidence: `formant.h:320 and 328 `auto fmt = std::dynamic_pointer_cast<IFormant>(src);`; additive_source2.h:82 `std::dynamic_pointer_cast<Partials>(src)`; full_additive_source.h:49, 56, 64, 67; delay_line_source.h:154 `if (auto* rs = dynamic_cast<RefSource*>(n))` inside prepare()`

Why: set_param has to guess the concrete type behind a shared_ptr<ValueSource> because interface pins are not typed; the loader silently drops mismatches (the UI's pin_type_compat_error exists to paper over this). dynamic_cast in prepare() also runs on the note-on path.

Recommendation: Add typed setters (`set_formant(std::shared_ptr<IFormant>)`) declared in the descriptor (InputDescriptor already carries a kind) so the loader dispatches without RTTI; cache the RefSource walk result at load.


## F163 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/source/additive/full_additive_source.h:18

**Stale tombstone comments describe code that no longer exists (registry alias, configurator, endHold_ member, retired UI windows)**

Reporters: arch-hacks-census

Evidence: `full_additive_source.h:18-20 `// The class name is FullAdditiveSource for file/header backward compat,
// but type_name() returns "AdditiveSource" and the old "FullAdditiveSource"
// name is kept as an alias in the registry.` — source_registrations.cpp registers only "AdditiveSource" (:90) and "BasicAdditiveSource" (:84); grep for FullAdditiveSource in that file hits only the comment and the constructor. source_registrations.cpp:83 `// BasicAdditiveSource (was "AdditiveSource" — alias kept for backward compat)` — no alias registered. source_registrations.cpp:484 `// Needs gains from JSON — create empty, configurator fills` — no configurator attached (:482-486); the loader branch at patch_loader.cpp:869-874 fills. envelope.h:542 `// endHold_: past the last stage, hold its endVal instead of emitting 0.` — no such member (:545-549).`

Why: Comments that assert an alias or a member exists send the next reader (or agent) to code paths that are not there; two of these describe loading behaviour a patch author might rely on ('FullAdditiveSource' in JSON will throw Unknown node type). The endHold note survives the 2026-08-13 kill of that mechanism. The class/file name mismatch (FullAdditiveSource vs type 'AdditiveSource') is itself a small ongoing cost.

Recommendation: Delete the four stale sentences; either register the alias the comment promises or (per the no-back-compat policy) drop the claim. Rename the class/file to AdditiveSource when a touch is due.


## F164 [UNVERIFIED] high (reporter: high) soundness — engine/include/mforce/source/additive/full_additive_source.h:49

**Structural pins (formant/partials/spectra) silently no-op when the wired source has the wrong interface — Parked.txt item 5 still live and undocumented in code**

Reporters: arch-hacks-census

Evidence: `auto fmt = std::dynamic_pointer_cast<IFormant>(src);
      if (fmt) formant_ = std::move(fmt);
      return;   // (same shape at :56-58 for IPartials; formant.h:320-321 and :328-329 for 'spectra'; additive_source2.h:82-83 for Partials)`

Why: Pins are typed by name only, so any ValueSource can be wired to 'formant'/'partials'/'spectra'. The cast fails, the wire is kept in the UI and the JSON, and the DSP renders with no connection and no message. Parked.txt:33-44 recorded this on 2026-04-20 ('No more lying wires'); nothing in the three set_param bodies references it, so a reader of the code cannot know the no-op is a known hole. A shipped editor that shows a connected wire that does nothing is a release-embarrassing defect class.

Recommendation: Make the structural mismatch loud: throw (or fprintf + reject) in set_param when the cast fails, and have the UI refuse the link at connect time using the registry's interface knowledge (Parked item 5b). Until then, add the Parked reference next to each `if (fmt)` so the gap is visible at the site.


## F165 [UNVERIFIED] medium (reporter: medium) smell — engine/include/mforce/source/additive/partials.h:62

**ExpandRuleNode exposes ten ValueSource params that are read exactly once at patch load; wiring an envelope to them in the UI silently does nothing**

Reporters: additive

Evidence: `// Snapshot to a plain ExpandRule for consumption by Partials. Reads the current() of each field — at load time these are ConstantSource values, at prepare time they could be the current value of a wired envelope.  ExpandRule to_struct() const { ... r.spacing1 = spacing1_->current(); ...   //  param_descriptors: {"spacing1", 0.5f, 0.0f, 24.0f, "semis"}, ... {"po2", 0.0f, 0.0f, 1.0f, "cycles"},`

Why: The only caller of to_struct() is patch_loader.cpp:482 (host->set_expand_rule(node->to_struct())) at load time; nothing re-snapshots at prepare, so the comment's 'at prepare time' path does not exist. Declaring these as ParamDescriptors (85-99) makes the UI draw ten wirable input pins whose connections are ignored, while count/recurse are correctly settings (128-134).

Recommendation: Declare spacing/dt/loPct/power/po as SettingDescriptor floats (they are scalar config, not signals) and drop the ValueSource members; or, if evolving expansion is actually wanted, re-snapshot in Partials::partials_prepare and document that only the note-on value is used.


## F166 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/additive/partials.h:175

**Descriptor spans re-declared in subclasses (macro splice in partials.h; frequency/amplitude/phase in 8 wave sources)**

Reporters: arch-build-headers, arch-duplication

Evidence: `partials.h `MFORCE_PARTIALS_MOTION_CONFIG_DESCS` defined 175-195 and spliced into the base at 305-317 and again into subclasses at 1077-1097, 1180-1199, 1264-1280, with rolloff/detune/bandwidth entries re-listed; frequency/amplitude/phase ParamDescriptors from dsp_wave_source.h:77-79 are re-declared in triangle 36-38, pulse 25-27, fm 37-39, wavetable 32-34, hybrid_ks 38-40, basic_additive 40-42, additive_source2 56-58, full_additive 30-32.`

Why: Descriptor spans are non-composable so every subclass copies the base list; the macro is a workaround that hides the duplication. A hint/range change on `frequency` is an 8-file edit, and partials subclasses can disagree with the base about the same setting.

Recommendation: Make descriptor accessors return a small composable view (base span + extension array concatenated via a constexpr helper, or a `std::vector` built once in the constructor) so subclasses only list what they add; drop the macro.

Also reported as: Settings descriptor tables are spliced with a preprocessor macro and base entries are re-declared in every Partials subclass; EvolutionSource wrappers hand-roll the same get/set if-chains eleven times


## F167 [UNVERIFIED] medium (reporter: medium) smell — engine/include/mforce/source/additive/partials.h:175

**Macro splices shared descriptor rows into four descriptor tables; descriptor names are otherwise hand-triplicated across descriptors/set_param/get_param with a documented drift bug**

Reporters: arch-modern-cpp

Evidence: `175: `#define MFORCE_PARTIALS_MOTION_CONFIG_DESCS \` used at 314, 1094, 1196, 1277; range_source.h:77-80 comment: "Default FALSE to match the loader's absent-key contract ... It was 1.0 here, so the UI displayed true for patches that omit the key and wrote true back on save — audibly changing them"`

Why: Every node repeats its parameter names as string literals in the descriptor table, in set_param/get_param/set_setting/get_setting if-chains and in the loader; the RangeSource bug is the measured cost of that triplication. A macro is the only reuse mechanism in use.

Recommendation: Replace the macro with a constexpr std::array base table and a constexpr concat helper (or std::span over a shared static array), and drive set/get dispatch from the descriptor table (index lookup + pointer-to-member) so a name exists exactly once per node.


## F168 [UNVERIFIED] low (reporter: low) elegance — engine/include/mforce/source/additive/partials.h:198

**IFormant and IPartials duplicate ValueSource's prepare/next under prefixed names, requiring forwarding shims in every implementer**

Reporters: additive

Evidence: `// Uses partials_prepare / partials_next to avoid collision with ValueSource::prepare / ValueSource::next.  ...  void prepare(const RenderContext& ctx, int frames) override { partials_prepare(ctx, frames); }  float next() override { partials_next(); return 0.0f; }   (281-282)`

Why: The stated reason is not a C++ constraint: a class deriving from ValueSource and an interface that declares the same virtual signature has a single final overrider. The result is seven shim pairs (formant.h:70-71, 128-129, 166-167, 234-235, 342-343; partials.h:281-282, 1430-1431) plus a latent double-advance hazard — a Formant that is both pulled as a graph node (ValueSource::next -> fmt_next) and driven by FullAdditiveSource (compute_wave_value -> fmt_next, full_additive_source.cpp:51) steps its parameter sources twice per sample.

Recommendation: Let IFormant/IPartials declare prepare/next with ValueSource's signatures (or derive them from ValueSource) and delete the shims; keep contains/get_gain and sum_partials as the only interface-specific methods.


## F169 [UNVERIFIED] high (reporter: high) god-class — engine/include/mforce/source/additive/partials.h:266

**Partials is an 800-line class with ~85 data members and seven distinct responsibilities**

Reporters: additive

Evidence: `struct Partials : ValueSource, IPartials {  ...  (member block lines 979-1065: baseSeed_/noteSeq_/noteBase_, 4 Randomizers + 3 vectors of Randomizer, 8 envelope shared_ptrs, 6 motion configs, 6 shimmer configs, trade/onset/decay/inharmonicity configs, 5 active flags, moWalks_/shWalks_/trWalks_, moVals_/shVals_/trVals_, bw* triple, 6 static arrays, 4 runtime arrays, 10 per-sample scalars, 3 caches + keys, expandRule_ + 6 orig* arrays, arrayUpdateReq_, rate_)`

Why: The class owns the descriptor/registry surface (27 settings, 8 params), the subclass array-generation protocol, expand-rule application, per-note rng stream management, six independent stochastic modulation layers (bandwidth, motion, shimmer, trade, onset, decay) each with its own prepare block and per-sample advance, two caches, and the render body. Each new layer has been added by appending another config/state/flag triplet and another if-block in partials_prepare, partials_next and partial_value_impl; the file is now the hardest unit in the engine to reason about and the layers cannot be tested or reused in isolation.

Recommendation: Extract value-type layer objects with prepare(n, noteBase, layerId)/advance()/gain(i) interfaces — CoherentWalkLayer (motion, shimmer), PairTradeLayer, BandwidthNoise, OnsetGate, DecayLaw — and a pure PartialExpander(ExpandRule, arrays) -> arrays. Partials then composes them and keeps only the render body and the array protocol; the setting-name dispatch can forward to each layer's own descriptor list.


## F170 [UNVERIFIED] medium (reporter: medium) elegance — engine/include/mforce/source/additive/partials.h:319

**Every node hand-writes the same pin plumbing five times per pin (descriptor, set_param, get_param, prepare forward, next forward, current read) — 44 set_param overrides of pure boilerplate**

Reporters: arch-valuesource-graph

Evidence: `partials.h:319-328 set_param (8 ifs), 330-340 get_param (8 ifs), 432-439 prepare forwarding (8 lines), 557-564 next forwarding (8 lines), 793-801 current reads;
ks_string.h:129-144 (set/get, 5 pins), 195-200 (prepare), 223-226 + 236 + 306 + 373 (next/current);
dsp_value_source.h:102-104: virtual std::span<const ParamDescriptor> param_descriptors() ... set_param ... get_param (the contract that forces it).
Grep: 44 files define `void set_param(std::string_view` (63 occurrences).`

Why: This is not the 'explicit registry instead of reflection' rule — the registry is the type table; this is per-pin dispatch code that every author must replicate correctly, and the project's history shows it is where things go wrong: CurveNode shipped with a descriptor but no wiring (patch_loader.cpp:756-765), Envelope's minValue/maxValue were silently unwired for four days (719-726), `set_param` on multi pins means append in two nodes and replace in the rest. The if-chains are also a linear string compare per call (fine at load, but push bindings call set_param per note). Concrete cost: ~50 lines per 8-pin node, no way to iterate a node's pins generically (promote_starved_refs and DelayLine::walk_to_tap must go through get_param by name for that reason).

Recommendation: Introduce a small non-reflective `Pin` type (`struct Pin { const ParamDescriptor* desc; std::shared_ptr<ValueSource> src; float v; }`) and a `PinTable` the node declares once (`Pin pins_[8]` + a constexpr name table). The base class implements param_descriptors/set_param/get_param/prepare-forwarding against the table; nodes write only `float next()` reading `pins_[kCutoff].pull()`. This keeps everything explicit and allocation-free, removes ~2,000 lines across the tree, fixes the double virtual call, and gives the loader/UI/walkers a real pin iterator. Roll out node-by-node behind the library null gate; Partials, KSString and the filters are the high-value first targets.


## F171 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/source/additive/partials.h:410

**Dead DSP setters: Partials::set_ro/set_dt, three Partials::setup overloads, MultiSource::set_weight/get_weight, BW registry configurators**

Reporters: arch-duplication

Evidence: `partials.h:410-411 `void set_ro(float ro1, float ro2)` / `set_dt` and `void setup(` at 1124, 1224, 1317 — no `setup(` or `set_ro(`/`set_dt(` call sites outside third_party (grep); multi_source.h:67-68 set_weight/get_weight unreferenced; source_registrations.cpp:420-431 BW configurators unreachable (see loader finding).`

Why: The comment at partials.h:409 says "Setters for backward compat / programmatic use" but the only loader path is set_array/set_setting; the setup overloads are a parallel configuration API that bypasses descriptors and can leave the object inconsistent with what the UI shows.

Recommendation: Delete; descriptors + set_array are the single configuration path (per the self-describing design).


## F172 [UNVERIFIED] high (reporter: high) workaround-hack — engine/include/mforce/source/additive/partials.h:442

**Expand rule mutates the source arrays in place and restores from an 'orig' snapshot, so config changes after the first prepare are silently discarded; ExplicitPartials adds a third copy (Stat_) to work around it**

Reporters: additive

Evidence: `if (hasExpand_) { if (origMult1_.empty()) { origMult1_ = mult1_; ... } else { mult1_ = origMult1_; mult2_ = origMult2_; ampl1_ = origAmpl1_; ... }  expandRng_ = ...; for (int r = 0; r <= expandRule_.recurse; ++r) apply_expand_rule(); }`

Why: Traced: FullPartials::set_setting("maxPartials") only sets arrayUpdateReq_ (line 1100); the next partials_prepare calls update_arrays() (428-430), which fills fresh mult1_, and then lines 447-451 overwrite it with the stale orig* snapshot — with an expand rule set, no later setting ever takes effect. Because the base get_array() (858-865) returns the post-expansion working arrays, ExplicitPartials keeps separate mult1Stat_/ampl1Stat_ copies (1393) and prefers them (1348-1351); but its set_setting("maxPartials") (1283) rewrites the working arrays via init_arrays_defaults() without touching Stat_, so the UI shows one set of partials while the render uses another. The lazy rebuild is also done inside a const getter via const_cast (858-859).

Recommendation: Make expansion a pure function: keep the subclass-generated arrays as the single source of truth and have partials_prepare expand them into separate render arrays (renderMult1_ etc.) every note. Drop orig* and Stat_. Rebuild the source arrays eagerly in set_setting/set_array (or mark the cache mutable) instead of const_cast in get_array.


## F173 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/source/additive/partials.h:459

**Dead state and dead interface surface: partialPO_ is sized but never read, BasicAdditiveSource has seven dead stores, IPartials::get_partial_value has no external caller**

Reporters: additive

Evidence: `partialPO_.assign(n, 0.0f);   (459; declared 1038, never read)  //  float ew = evenWeight_->next(); ... float avs = amplVarSpeed_->next() / float(sampleRate_);  ew = evenWeight_->current(); ...   (basic_additive_source.h:97-111)  //  virtual float get_partial_value(float amplitude, float frequency, float phaseDiff, int index, IFormant* formant, float formantWeight, float formantFloor) = 0;   (207-209)`

Why: partialPO_ costs an allocation and a vector per note for nothing. The BasicAdditiveSource block assigns seven locals from next() and immediately overwrites them from current() — the next() calls are needed, the stores are noise. get_partial_value is called only by the interface's own default loop (229) and by CompositePartials' O(sets)-per-partial forwarder (1449-1462); the 'single-partial callers' the comment at 604 anticipates do not exist in the repo, yet every implementer must provide it and Partials pays an ensure_partial_cache() per call.

Recommendation: Delete partialPO_; rewrite the BasicAdditiveSource block as bare next() calls followed by the current() reads; remove get_partial_value from IPartials and keep sum_partials as the sole render entry.


## F174 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/additive/partials.h:499

**Motion and shimmer layers are copy-pasted: identical prepare blocks, identical advance loops, parallel member triplets**

Reporters: additive

Evidence: `motionActive_ = (moDepth1_ != 0.0f || moDepth2_ != 0.0f); if (motionActive_) { moLen_ = ...; moSharedRng_ = Randomizer(stream_seed(noteBase_, 0, kRngMotionShared)); walk_init(moShared_, ...); moWalks_.assign(n, MotionWalk{}); ... }   (499-511)  vs  shimmerActive_ = (shDepth1_ != 0.0f || shDepth2_ != 0.0f); if (shimmerActive_) { shLen_ = ...; shSharedRng_ = ...; walk_init(shShared_, ...); shWalks_.assign(n, MotionWalk{}); ... }   (512-524)`

Why: The two blocks differ only in prefix; the same holds for the per-sample advance loops (569-576 vs 577-584: 'moVals_[i] = coh * shared + (1.0f - coh) * ind;' / 'shVals_[i] = coh * shared + (1.0f - coh) * ind;') and for the config/state members (1009-1012, 1026-1029, 986-987). Any fix to the coherence mix, segment logic or seeding has to be made twice, and the trade layer (525-532, 585-588) is a third near-copy.

Recommendation: Introduce one CoherentWalkLayer { depth1, depth2, hz, coherence, evolve; shared walk + rng; per-partial walks + rngs + vals; prepare(n, noteBase, sharedId, indId); advance(); } instantiated twice (motion, shimmer) — this is the first extraction step of the god-class finding.


## F175 [UNVERIFIED] medium (reporter: medium) performance — engine/include/mforce/source/additive/partials.h:818

**Per-partial cache rebuilds every sample with std::pow per partial whenever multEnv or roEnv actually moves — the documented purpose of those params**

Reporters: additive

Evidence: `if (partialCacheValid_ && sMultE_ == cachedMultE_ && sRoE_ == cachedRoE_) return;  ...  rolloffCache_[i] = (ro == 0.0f) ? 1.0f : (1.0f / std::pow(pmult, ro)); if (scale) moScaleCache_[i] = std::pow(pmult, moScale_); if (decayActive_) { const double rate = double(decayRate_) * std::pow(double(pmult), double(decayExp_)); decayGain_[i] = float(std::pow(10.0, -rate / (20.0 * double(rate_)))); }`

Why: The cache is keyed on exact float equality of both scalars (line 819). The param_descriptors comment (289-291) tells users to wire an Envelope to multEnv/roEnv, and patches/baselines/fadd_explicit_test.json does (multEnv -> ADSR). While that envelope moves, every sample pays N × std::pow (plus one more for moScale and two double pows for decay) — the exact CRT-call-per-partial cost the rest of this file measured and removed. The comment at 816-817 acknowledges the cliff rather than closing it.

Recommendation: Split the keys: pmult/decayGain depend only on multE, rolloff on (multE, roE). Cache log2(pmult) per partial and compute rolloff as fast_exp2(-ro * log2pmult) so a moving roE costs one exp2 per partial; recompute decayGain only when pmult changes.


## F176 [UNVERIFIED] medium (reporter: medium) modern-cpp — engine/include/mforce/source/additive/partials.h:859

**const member mutates through const_cast instead of a mutable cache**

Reporters: arch-modern-cpp, arch-valuesource-graph

Evidence: `if (arrayUpdateReq_) const_cast<Partials*>(this)->update_arrays();`

Why: A const accessor that rewrites member arrays is a hidden write; the codebase has zero `mutable` members, so the lazy-cache idiom is expressed by casting rather than by type. Calling it on a genuinely const Partials is UB.

Recommendation: Declare the lazily-rebuilt arrays and the flag `mutable`, or make get_array non-const.

Also reported as: Cold reflection methods on the hot ValueSource interface do work they should not: get_array() mutates through const_cast, get/set_array copy vectors by value


## F177 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/additive/partials.h:1087

**Base Partials setting descriptors are hand-copied into all three subclass lists, with only the newer entries spliced by macro**

Reporters: additive

Evidence: `{"rolloff1", SettingType::Float, 1.0f, 0.0f, 10.0f}, {"rolloff2", ...}, {"detune1", ...}, {"detune2", ...}, {"bandwidth1", ...}, {"bandwidth2", ...}, {"bandwidthHz", SettingType::Float, 30.0f, 1.0f, 2000.0f}, MFORCE_PARTIALS_MOTION_CONFIG_DESCS`

Why: The same seven entries appear in Partials (307-313), FullPartials (1087-1093), SequencePartials (1189-1195) and ExplicitPartials (1270-1276); the 20-entry tail is a preprocessor macro (175-195) precisely because the copies had drifted. A range or default change to rolloff/detune/bandwidth must be made in four places, and the macro hides descriptor content from tooling and from readers.

Recommendation: Replace the macro and the copies with a constexpr concatenation (e.g. a small constexpr std::array concat helper) so each subclass declares only its own entries and composes base + own once in a static; or have the base implement setting_descriptors() as base-list + virtual own_setting_descriptors().


## F178 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/allpass_resonator.h:131

**AllpassResonator captures frequency once per note but does not override tracks_frequency_live()**

Reporters: sources-physical

Evidence: `line 131 `if (!initialized_) init_note();` with line 185 `float f0 = frequency_ ? frequency_->current() : 220.0f;` — the same lazy-capture pattern as KSString, which declares it at ks_string.h:70 `bool tracks_frequency_live() const override { return false; }`. AllpassResonator has no such override, so the base default (dsp_value_source.h:90) returns true.`

Why: patch_loader.cpp:1249 and :1396 use tracks_frequency_live() to warn by name when a bend/articulation is wired to a node that ignores it. For AllpassResonator the warning is silent while the bend is just as inert as on KSString — the loader's safeguard lies for this node.

Recommendation: Add `bool tracks_frequency_live() const override { return false; }` to AllpassResonator. Longer term, factor the shared 'lazy note init + frequency snapshot' behaviour into one small base so the flag cannot be forgotten on the next node.


## F179 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/allpass_resonator.h:219

**AllpassResonator lacks KSString's top-of-keyboard and Thiran-eta guards**

Reporters: sources-physical

Evidence: `line 219 `M = std::clamp(M, 2.0f, float(kBufLen - 4));` and lines 221-222 `float frac = M - float(loopLen_); tuneA_ = (1.0f - frac) / (1.0f + frac);`. KSString handles both cases: dispersion shedding by bisection at ks_string.h:470-494 ('up to -40 cents mistuning, per-note ring/dead chaos across octave 8') and `if (frac < 0.1f) { M -= 1; frac += 1.0f; }  // keep eta away from 1` at :555.`

Why: At the descriptor's 8 kHz ceiling the period is 6 samples; apDelay (2x(1+a)/(1-a) ~2.7 at stiffness 0.15) + lpDelay + 1 already exceeds the budget, so M clamps to 2 and the note is silently flat — the same failure KSString documented and fixed. Separately, when frac lands near 0 (about 5-10% of notes, uniformly distributed) tuneA_ -> 1 puts the tuning allpass pole on the unit circle at z=-1; rounding error in that recursion neither grows nor decays, which is a numerical hazard inside a loop with feedback up to 0.9999.

Recommendation: Port both guards: shed stiffness by bisection when `period - apDelay - lpDelay - 1 < kMinLoop`, and shift the fractional part into [1.0, 1.1) before computing tuneA_ (M -= 1). Both are one-note-init-time, zero render cost.


## F180 [UNVERIFIED] low (reporter: low) efficiency — engine/include/mforce/source/bow_table_source.h:81

**Per-sample std::pow for an integer power, and runtime modulo in DelayLine's inner loop**

Reporters: sources-physical

Evidence: `bow_table_source.h:81 `float rc = std::pow(s, -4.0f);` and ks_string.h:310 `float fricCoef = std::pow(bt, -4.0f);` per sample; delay_line_source.h:118 `const int i1 = (i0 + 1) % int(buf_.size());` and :124 `writeIdx_ = (writeIdx_ + 1) % int(buf_.size());` — two integer divisions by a runtime divisor per sample (KSString's `% kBufLen` is a constexpr power of two and compiles to a mask).`

Why: A libm pow call is ~20-50x the cost of `b2 = s*s; 1.0f/(b2*b2)` and sits in the only per-sample work BowTable does; idiv is ~20-40 cycles on x86 versus a compare-and-wrap. Small individually, but these are the nodes meant to run many-per-voice in bowed/loop patches.

Recommendation: Replace pow(x,-4) with one square, one square, one reciprocal (or share it via the Friedlander primitive). In DelayLine use `if (++writeIdx_ >= size) writeIdx_ = 0;` and `i1 = i0 + 1 == size ? 0 : i0 + 1`.


## F181 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/source/combined_source.h:17

**combined_source.h is a grab-bag of five unrelated nodes**

Reporters: sources-combinators

Evidence: ``struct CombinedSource` (17), `struct CrossfadeSource` (121), `struct DistortedSource` (234), `struct StaticVarSource` (331), `struct StaticRangeSource` (357)`

Why: Only the first two are combiners; DistortedSource is a Modulator waveshaper (the natural sibling of ShaperSource in shaper_source.h) and the Static* pair are Utility constants. The UI menu labels 'Distortion'/'Crossfade'/'Phased' (main.cpp:10874/10886/10800) and the baseline distortion_test.json resolve to classes a reader cannot find by file name, and the file carries three unrelated include sets.

Recommendation: Split: distorted_source.h next to shaper_source.h; static_sources.h (or fold into a utility header beside ConstantSource); leave combined_source.h for Combined + Crossfade until Crossfade is retired.


## F182 [UNVERIFIED] low (reporter: low) duplication — engine/include/mforce/source/combined_source.h:59

**CombineOp ordinal/label/string mapping maintained in three places that 'must stay in step'**

Reporters: sources-combinators

Evidence: `combined_source.h:59-61 `kOpLabels[] = { "Mix", "Multiply", "Fade", "Sum", nullptr };` :70 `op = static_cast<CombineOp>(int(value));` source_registrations.cpp:278-282 (ordinal switch) and :291-294 (string chain), with the admission at :274-275 'Ordinals must stay in step with CombineOp and the kOpLabels dropdown'.`

Why: The configurator sets `cs.op`/`cs.gainAdj` directly instead of going through set_setting, so the enum appears as a label table, an unchecked cast, an ordinal switch and a string chain; adding an op means editing four spots and the cast at :70 accepts any int the UI sends.

Recommendation: Make the label table the single source of truth: expose it (or iterate setting_descriptors().enum_labels) and have the configurator map string->index->set_setting; range-check the cast in set_setting against the descriptor max.


## F183 [UNVERIFIED] low (reporter: low) soundness — engine/include/mforce/source/combined_source.h:70

**Unchecked float-to-enum conversion for the operation setting**

Reporters: arch-modern-cpp

Evidence: `if (name == "operation") { op = static_cast<CombineOp>(int(value)); return; }`

Why: Any out-of-range setting value (UI drag, hand-edited JSON) yields a CombineOp with no enumerator; the switch on it then falls to whatever default exists, silently changing the sound.

Recommendation: Clamp/validate against the enum_labels count from the descriptor before casting.


## F184 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/combined_source.h:101

**Three hand-rolled linear crossfades; CrossfadeSource is the two-stage case of PhasedValueSource**

Reporters: sources-combinators

Evidence: `combined_source.h:103 `cur_ = v1 * (1.0f - t) + v2 * (1.0f + gainAdj) * t;` combined_source.h:214 `cur_ = (v1 * (1.0f - t) + v2 * (1.0f + gainAdj) * t) * amp;` phased_value_source.h:140 `cur_ = (vPrev * (1.0f - t) + vCurr * stages_[stageIdx].gainAdj * t) * amp;``

Why: CombinedSource::Fade (101-104), CrossfadeSource::next (206-215) and PhasedValueSource::next (135-150) each implement the same fade with their own zone arithmetic and their own gain convention ((1+gainAdj) in the first two, a raw multiplier in Phased). CrossfadeSource (ratio + overlap, two sources) is structurally a two-stage PhasedValueSource; the legacy code composed Phased FROM CombinedSource(Fade) (PhasedValueSource.cs:154-158), and inlining it in the port is where the boundary-restart bug above was introduced. Three copies means three places to get the boundary math right and three gain semantics for the UI to explain.

Recommendation: One inline helper `crossfade(a, b, t, gainB)` shared by all three, and either express CrossfadeSource as PhasedValueSource with two stages or retire it (no patch back-compat rule) after migrating the handful of patches that use it.


## F185 [UNVERIFIED] high (reporter: high) soundness — engine/include/mforce/source/combined_source.h:198

**CrossfadeSource and PhasedValueSource crash/UB on an incompletely wired graph; siblings guard**

Reporters: sources-combinators

Evidence: `combined_source.h:198 `source1_->prepare(ctx, s1Count + overlapCount_);` with members at 222-223 `std::shared_ptr<ValueSource> source1_; std::shared_ptr<ValueSource> source2_;` (no initializer, no ctor). phased_value_source.h:128 `int stageLen = boundaries_[stageIdx] - prevBound;` with no empty check.`

Why: CrossfadeSource is created bare by the registry (source_registrations.cpp:304 `std::make_shared<CrossfadeSource>()`) and by the UI (main.cpp:462 `reg.create(typeName, ...)`), and the UI only calls set_param for pins that carry a constantSrc (main.cpp:463-466) or on connect (wire_pin, :470-473) — InputDescriptor pins such as source1/source2 are left null. A Crossfade node reachable from the output with one source unwired null-derefs in prepare()/next(). PhasedValueSource has the same exposure in the other direction: the UI menu offers it (main.cpp:10800) but the class exposes no InputDescriptor(multi)/add_param for stages, so a UI-made instance has zero stages and next() indexes boundaries_[0]/stages_[0] on empty vectors (UB) on the first sample. Compare the conventions right beside them: CombinedSource is registered with ConstantSource placeholders (source_registrations.cpp:262-265) and DistortedSource null-checks every pin (combined_source.h:286-305).

Recommendation: Give CrossfadeSource a default constructor seeding source1_/source2_ with ConstantSource(0) (as CombinedSource's registration does) or null-check in prepare/next; have PhasedValueSource::next() return 0 when stages_ is empty, and expose stages through the multi-input contract (see the self-description finding) so a UI instance can actually be completed.


## F186 [UNVERIFIED] medium (reporter: medium) smell — engine/include/mforce/source/combined_source.h:331

**StaticVar/StaticRange/PhasedValueSource bypass the self-describing contract via registry configurators poking public fields**

Reporters: sources-combinators

Evidence: `combined_source.h:332-334 `float baseValue{1.0f}; float varPct{0.0f}; float bias{0.0f};` with no setting_descriptors/set_setting; phased_value_source.h:44-49 declares only `{"amplitude", ...}`; source_registrations.cpp:326-328 `sv.baseValue = p.value("baseValue", 1.0f); ...`, :399-409 builds stages ad hoc.`

Why: The architecture is explicit registry + self-describing nodes (type_name/descriptors/set_param/set_setting) so the UI, serializer and loader share one surface. These three nodes carry their real configuration (baseValue/varPct/bias, min/max/bias, overlap/stages/percent/min/max/gain) only in loader-side lambdas, so the UI cannot edit or wire them (docs/config_pin_census.md:127,138-139 show empty columns), generic save cannot round-trip them, and the UI can create an unusable Phased node (see the crash finding). CombinedSource/CrossfadeSource show the intended pattern right above them.

Recommendation: Give StaticVarSource and StaticRangeSource SettingDescriptors + set/get_setting (and a default ctor); give PhasedValueSource a SettingDescriptor for overlap and a multi InputDescriptor 'stages' with add_param/clear_param, moving the per-stage scalars to either an ArrayDescriptor group or per-stage sub-settings; shrink the configurators to pure JSON-shape adapters.


## F187 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/delay_line_source.h:25

**Buffer is sized for ratio 1 at 20 Hz while the ratio pin advertises up to 20x, so long delays silently clamp**

Reporters: sources-physical

Evidence: `line 25 `buf_(size_t(sr / 20 + 4), 0.0f)`; line 40 descriptor `{"ratio", 1.0f, 0.05f, 20.0f, "ratio"}`; line 112-113 `const float len = std::clamp(float(sampleRate) / f * ratio_->current() - comp, 1.0f, float(buf_.size()) - 2.0f);`. Also line 17 `int sampleRate{48000};` is a public mutable member that the buffer size does not follow.`

Why: At 100 Hz with ratio 20 the node needs 9600 samples and has 2402: the 'pitch-tracked' delay lands two octaves high with no diagnostic. Any patch that uses ratio for sub-harmonic or long-loop resonators below ~400 Hz hits this. The public sampleRate member invites the same silent mismatch if anything writes it after construction (buffer is not resized).

Recommendation: Size buf_ for `sr / 20 * maxRatio + 4` (192 KB at 48k — acceptable for a resonator backbone) or lower the ratio descriptor max to what the buffer supports and clamp in next() with a one-shot stderr note. Make sampleRate a private const like the sibling nodes.


## F188 [UNVERIFIED] low (reporter: low) soundness — engine/include/mforce/source/delay_line_source.h:167

**Loop walk silently drops members past the cap and frees shared_ptrs under the audio lock**

Reporters: sources-physical

Evidence: `line 167 `if (found && nMembers_ < kMaxMembers) members_[nMembers_++] = sp;` — overflow is a silent skip; the comment at lines 132-133 records that exactly this silent eviction produced '-85 cents' at cap 8. Line 91 `for (auto& m : members_) m.reset();` on shared_ptrs that are held precisely so 'a UI rewire between walks can't dangle the render thread' (lines 139-140), i.e. the DelayLine may be the last owner.`

Why: Raising kMaxMembers from 8 to 16 kept the failure mode; a 17-member loop will mistune silently again. And when the DelayLine is the last owner of a rewired node, reset() in prepare() runs that node's destructor (vector frees) inside the g_audioMutex section the RtAudio callback is waiting on (mforce_ui/main.cpp:4676-4692).

Recommendation: On overflow set cycleFound_=false and emit a one-shot stderr line naming the node (consistent with the loader's 'keep the default LOUDLY' policy). For the release, move expired members to a UI-side garbage list drained outside the lock, or hold raw pointers plus a graph generation counter.


## F189 [UNVERIFIED] medium (reporter: medium) rt-safety — engine/include/mforce/source/fm_source.h:74

**Oversampled FM rebuilds its decimation filter bank with heap allocations on every prepare (per note)**

Reporters: sources-combinators

Evidence: `line 74 `decimSections_.clear();` lines 79-80 `for (int i = 0; i < nSec; ++i) decimSections_.emplace_back(float(i + 1), float(nSec * 2), subRate);``

Why: BWLPSection owns FIRFilter fir{3} and IIRFilter iir{2}, each a std::vector (filters.h:14,16,34,36), so every prepare() with oversample>1 performs the vector growth plus eight vector allocations, per voice per note. The section count is a compile-time constant (line 78 `const int nSec = 4;`) so none of this needs to be dynamic. Secondary: set_setting("oversample") (lines 95-98) changes M immediately but the bank is only rebuilt at the next prepare, so a live change 1->4 runs the M>1 loop (line 160-161) over an empty bank, i.e. oversampled but unfiltered, until the next note.

Recommendation: Hold `std::array<BWLPSection, 4>` (or a fixed-capacity member) constructed once; in prepare() call update(cutoff) and reset the FIR/IIR state (add a reset() to those filters — filters unit). Rebuild/refresh the bank in set_setting("oversample") as well, or mark dirty and refresh at the top of compute_wave_value when M changed.


## F190 [UNVERIFIED] low (reporter: low) efficiency — engine/include/mforce/source/fm_source.h:110

**Per-sample double virtual call (next() result discarded, current() re-read) and unused base-class phase bookkeeping**

Reporters: sources-combinators

Evidence: `fm_source.h:110-117 `carrierRatio_->next(); modRatio_->next(); depth_->next(); ... carrierRatio_->current(); ... modRatio_->current(); ... depth_->current();` combined_source.h:89-93 and 295-305 same shape; shaper_source.h:163-164 `smoothness_->next(); smoothCur_ = smoothness_->current();`; comma idiom at combined_source.h:204 and phased_value_source.h:116 `(amplitude_->next(), amplitude_->current())`.`

Why: ValueSource::next() returns the new value (dsp_value_source.h:78; ConstantSource::next at :136), so each parameter costs two virtual calls per sample instead of one: DistortedSource pays five extra, FM three. Separately, FMSource keeps its own carrierPhase_ (line 152-153) and never reads currPos_, yet WaveSource::next() still runs the fmod/phaseDelta accumulator for it every sample (dsp_wave_source.h:56-60). This is an engine-wide convention (WaveSource::next does the same at :33-47), so the fix is a one-time sweep, not a unit patch.

Recommendation: Adopt `const float v = src->next();` throughout; for FM, either make the base-class phase accumulation opt-out for subclasses that own their phase, or drop FM's private accumulator and consume currPos_.


## F191 [UNVERIFIED] medium (reporter: medium) god-class — engine/include/mforce/source/ks_string.h:56

**KSString bundles four mechanisms, 21 settings and five pins in one 636-line class**

Reporters: sources-physical

Evidence: `setting_descriptors() lines 92-124 list 21 settings; next() lines 222-376 interleaves the damper-noise generator (:244-256), excitation DC blocker (:259-261), inharmonics nested-allpass loop (:267-294), bow junction (:304-316), stagger history (:320-331) and the comb bank (:322-362); set_setting/get_setting are two 21-way string ladders (:146-193).`

Why: The header's own justification — feedback loops must live inside one node — explains why the comb bank and inharmonics loop share a class, but the bow junction duplicates BowTableSource and the damper-contact noise is an independent generator. Every new feature lands in next() and in two string ladders; the init_note ordering bug above is the kind of thing that hides in a 125-line init with six interdependent per-note quantities.

Recommendation: Keep KSString as the loop container but make next() orchestration over small POD sub-objects with their own tick/reset: Comb (already a struct — give it tick()), InharmLoop, BowJunction (built on the shared Friedlander primitive), DamperNoise. Bit-identical by construction; each piece becomes testable on its own and the two string ladders shrink to the loop-level knobs.


## F192 [UNVERIFIED] low (reporter: low) efficiency — engine/include/mforce/source/ks_string.h:62

**KSString allocates its maximum configuration (~200 KB) per instance regardless of numCombs**

Reporters: arch-build-headers

Evidence: `ks_string.h:62-63 `for (int i = 0; i < kMaxCombs; ++i) comb_[i].buf.assign(kBufLen, 0.0f); disp_.buf.assign(kBufLen, 0.0f);` with :385-386 `kMaxCombs = 10; kBufLen = 4096;`, :611 `Comb comb_[kMaxCombs];`, :618/:623 `kInHist = 1024; float inHist_[kInHist] = {};` — 10*4096*4 + 4096*4 + 1024*4 ≈ 184 KB per node`

Why: Default patches use 3 combs (:591) but every voice in a pool (and every Multiplex clone) carries the 10-comb footprint; prepare() zero-fills all ten buffers (:202-208) per note-on, 160 KB of memset on the UI/audio critical section per key press.

Recommendation: Allocate `numCombs_` buffers at construction from the patch's numCombs setting (the loader applies settings before prepare), or zero-fill only the first `numCombs_` combs in prepare().


## F193 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/ks_string.h:156

**Stability-critical settings are unclamped in set_setting, and the loader does not clamp against descriptors**

Reporters: sources-physical

Evidence: `line 156 `if (name == "inharmHp")   { inharmHp_   = v; return; }` and line 160 `if (name == "fbCoeff")    { fbCoeff_    = v; return; }` sit between neighbours that all std::clamp. inharmHp feeds line 577 `hpR_ = std::exp(-2.0f * 3.14159265f * inharmHp_ / sr);` which is the pole of the in-loop highpass at line 288 `float hp = hpR_ * (disp_.hpY + y1 - disp_.hpX);`. patch_loader.cpp:417 applies JSON values with `src.set_setting(desc.name, fv);` and has no min_value/max_value check (grep: none in the file).`

Why: A negative inharmHp in a patch file gives hpR_ > 1: a pole outside the unit circle inside a feedback comb that is itself inside the global feedback loop — unbounded growth, not just a bad sound. fbCoeff > 1 likewise defeats the headroom normalisation at line 574. The descriptors advertise 10..5000 and 0..0.9 but nothing enforces them on the JSON path, and patches here are written by sweep scripts and hand edits as much as by the UI.

Recommendation: Clamp every setting in set_setting to its descriptor range (the class already does this for 15 of 21), or better, clamp generically once in patch_loader's setting loop using SettingDescriptor min/max so every node gets it. The generic fix removes the per-node ladder as a failure point.


## F194 [UNVERIFIED] low (reporter: low) performance — engine/include/mforce/source/ks_string.h:202

**prepare() zero-fills the maximum-capacity state, not the used portion, inside the audio-critical section**

Reporters: sources-physical

Evidence: `ks_string.h:202-208 `for (int i = 0; i < kMaxCombs; ++i) { std::fill(comb_[i].buf.begin(), comb_[i].buf.end(), 0.0f); ...}` — 10 x 4096 + 4096 floats (~180 KB) per note-on with default numCombs_=3. mesh2d_source.h:348-355 clear_mesh() zeroes 9 x 64 x 64 floats (~147 KB) per note with a default 12x12 mesh (~5 KB live).`

Why: prepare() runs at every note-on inside g_audioMutex while the RtAudio callback blocks (mforce_ui/main.cpp:4676-4692; instrument.h:577-579). ~180 KB memset per voice is ~15-20 us; a ten-note chord is a few hundred microseconds of callback stall for memory the render loop never reads. Bounded, so not an RT violation, but pure waste that scales with polyphony.

Recommendation: Clear only comb_[0..numCombs_) in KSString and [0..NX_)x[0..NY_) in Mesh2D; set_setting already clears everything when the geometry changes, so the unused region stays zero. Alternatively keep a 'dirty' flag and skip the fill for combs never written.


## F195 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/ks_string.h:253

**48 kHz-baked per-sample constants (backlog 69) and the WaveEvolution sample-rate audit (Parked.txt item 3) are still open in code**

Reporters: arch-hacks-census

Evidence: `ks_string.h:253 `dnAmp_ *= 0.9995f;   // -60 dB over ~0.29 s at 48 kHz` and :369 `envFollow_ = acm > envFollow_ ? acm : envFollow_ * 0.99995f;`; wave_evolution.h:42-45 PluckEvolution::adjust `sampleCount_ = float(std::round(std::max(24.0 - 4.0 * logF * logF, 2.0))) + muting_ * 20.0f;` (sample counts, no sampleRate_), :258-273 EKSEvolution::adjust and :357-360 ReedEvolution::adjust likewise; only BowedString (:423) and Brass (:522) implement on_prepare.`

Why: The engine is rate-parametric everywhere else (patches carry sampleRate; reverb scales from 44.1k at reverb.h:72), so a 44.1 kHz export or variable-rate live path would change damper-noise decay, follower time constants and pluck/EKS/reed character silently — the 'STK disease' the backlog names. Parked.txt:5-11 (2026-04-20) and dsp/BACKLOG.md:69-80 both document it; nothing in the code marks the constants as rate-baked except one comment.

Recommendation: Derive the two KSString coefficients from sr (exp(-1/(tau*sr))) and give Pluck/EKS/Reed an on_prepare that scales their sample counts by sr/48000; null-gate at 48k for byte identity. Until then, tag each baked constant with the backlog id so the audit can grep them.


## F196 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/source/ks_string.h:271

**Inner `float d` shadows the damper value `d` in next()**

Reporters: arch-build-headers, sources-physical

Evidence: `line 235 `float d = 0.0f;` (damper, used at 236-256), then inside the inharmonics block line 271 `float d = disp_.buf[rd];` and line 291 `inh = d;`.`

Why: Correct today only because nothing inside the block means the damper. Any future edit that reads 'd' there (e.g. applying damp to the inharm input, which already uses `damp` at 290) silently gets the delay sample. MSVC /W4 C4456 would flag it.

Recommendation: Rename the delay read to `tap` or `dl`.

Also reported as: Local variable `d` shadows the damper amount `d` inside KSString::next()


## F197 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/ks_string.h:386

**Buffer sizes and time constants are baked for 48 kHz while sibling nodes derive from sr**

Reporters: sources-physical

Evidence: `ks_string.h:386 `static constexpr int kBufLen = 4096;  // >= sr/12Hz at 48k`, :538 `comb_[i].len = std::clamp(len, 2.0f, float(kBufLen - 4));`, :618 `kInHist = 1024;  // 21 ms @ 48k`, :253 `dnAmp_ *= 0.9995f;   // -60 dB over ~0.29 s at 48 kHz`, :369 `envFollow_ * 0.99995f`; allpass_resonator.h:174 `kBufLen = 4096` with :219 clamp. Contrast delay_line_source.h:25 `buf_(size_t(sr / 20 + 4), 0.0f)` and mesh2d_source.h:201 `sr_ = float(ctx.sampleRate > 0 ? ctx.sampleRate : 48000);`.`

Why: At 96 kHz the KSString/AllpassResonator descriptor minimum of 12 Hz (ks_string.h:75) silently clamps to ~23.5 Hz with no diagnostic, excStagger is capped below its advertised 20 ms, and the damper-noise/envelope-follower decays halve in duration. Within one unit three conventions coexist (constexpr 48k, ctor sr, ctx.sampleRate with a 48000 fallback) — the STK port notes already record a '48k baseline bug' from exactly this pattern. CLAUDE.md says plan for cross-platform; these are the places that will break first.

Recommendation: Size comb_/disp_/buf_ from sampleRate_ in the constructors (as DelayLine does: `sr/12 + margin`), size inHist_ from `sr * 0.02`, and compute the two decay coefficients in init_note from sr. Pick ONE rate source for the unit — the constructor int, since every ctx passed is the same value — and have Mesh2D take sr in its ctor like its siblings instead of reading ctx with a fallback.


## F198 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/ks_string.h:426

**Waveguide primitives (tuning allpass, DC blocker, phase-delay solvers, KS averaging, fill_table) re-typed per node**

Reporters: arch-duplication

Evidence: `ks_string.h biquad_ap 426-433 vs allpass_resonator.h 154-159; ap1_phase_delay 438-444 vs 178-182 (verbatim); LP phase delay 496-501 vs 201-206; DC blocker 259-260/578 vs 134-135/224; init_note prologue 458-462 vs 185-189. Thiran tuning allpass also at wavetable_source.cpp:88-94 and 134-140 and wave_evolution.h:324-330; wavetable compute_raw 44-61 vs compute_interpolated 100-115 share first-sample init + coefficient code; 2-sample KS average at hybrid_ks_source.cpp:42-47, wave_evolution.h:368-370, 213-214, 557-560; fill_table wavetable_source.cpp:21-32 vs hybrid_ks_source.cpp:32-40.`

Why: ~40 verbatim lines between ks_string.h and allpass_resonator.h alone, plus 5 more copies of the fractional-delay tuning coefficient. The junction-state / hysteresis / 2D work in the roadmap touches exactly these primitives; each change is a multi-file hunt and the copies already differ in precision and clamp handling.

Recommendation: Add core/filter_primitives.h with POD structs the way pierce_allpass.h already does: `OnePoleDC`, `ThiranAP1 {set_delay(frac); tick()}`, `BiquadAP`, `ap1_phase_delay()`, `lp_phase_delay()`, `ks_average(a,b)`, `fill_table(span, rng)`; replace the inline copies.


## F199 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/ks_string.h:438

**Four DSP primitives are copy-pasted across KSString, AllpassResonator, BowTable and wave_evolution**

Reporters: sources-physical

Evidence: `(1) first-order allpass phase delay: ks_string.h:438-444 `ap1_phase_delay` and allpass_resonator.h:178-182 `ap1_phase_delay`, identical math. (2) double-real-pole allpass biquad: ks_string.h:426-433 `biquad_ap` vs allpass_resonator.h:155-159 `ys = a*a*yt - 2.0f*a*bq_.x1 + bq_.x2 + 2.0f*a*bq_.y1 - a*a*bq_.y2;`. (3) one-pole LP phase delay: ks_string.h:496-501 vs allpass_resonator.h:202-206. (4) 20 Hz DC blocker: ks_string.h:259-260 + :578 vs allpass_resonator.h:134-135 + :224, and a second copy inside KSString for the bow at :313-314. (5) Friedlander friction: ks_string.h:309-311 `std::fabs(frictionGain_*vRel)+0.75f; std::pow(bt,-4.0f); if (fricCoef > 0.98f)`, bow_table_source.h:80-82, wave_evolution.h:465-468.`

Why: Each copy already drifts: the KSString bow has a hard-coded 0.98 ceiling and no floor while BowTable has both as settings; KSString's biquad is a method while AllpassResonator's is inline; the phase-delay helpers are what DelayLine's phase_delay_at protocol needs and today only Biquad/SVF implement it — KSString and AllpassResonator cannot report their in-loop delay because the math is private to each. Fixes to one copy (the init_note ordering bug above, an eta guard) do not reach the others.

Recommendation: Add core/dsp_primitives.h with POD structs: DcBlocker{r,x,y; tick(); set_hz(sr)}, OnePoleLP{c,y; tick(); phase_delay_at(w)}, Allpass1{a,x,y; tick(); phase_delay_at(w)}, Allpass2Pole{a,state; tick(); phase_delay_at(w)}, Friedlander{slope,offset,min,max; rc(dv)}. Header-only, inline, no virtuals — bit-identical to the current code and ~60 lines removed. Then BowTableSource and the KSString bow junction share one friction function.


## F200 [UNVERIFIED] low (reporter: low) testing — engine/include/mforce/source/ks_string.h:457

**No regression tests for KSString, AllpassResonator, Mesh2D, BowTable or HybridKS**

Reporters: sources-physical

Evidence: `engine_tests/main.cpp covers DelayLine (:226, :322), Wormhole (:498) and PierceFilter (:675); grep for KSString|AllpassResonator|Mesh2DSource|BowTableSource|HybridKSSource across tools/engine_tests finds nothing. KSString's init_note (:457-581) makes four closed-form tuning claims (comb length, allpass eta, dispersion shed, fb normalisation) with no measured-pitch check.`

Why: The init_note ordering bug reported above is detectable by a 20-line test in the existing measured-period style; without one, every retune of these nodes is validated only by ear via the audition queue, which CLAUDE.md says is Matt's scarce resource.

Recommendation: Add a pitch-accuracy test per tuned node (render 0.5 s at C2/A4/C7/B8, autocorrelate, assert within +-3 cents) and a 'same note after different predecessor is byte-identical' determinism check for KSString.


## F201 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/ks_string.h:483

**init_note reads lpDelay_ in the dispersion-shedding guard before computing it**

Reporters: sources-physical

Evidence: `line 483: `if (dispActive_ && minPeriod - apDelay - lpDelay_ < kMinCombLen) {` and line 488 `if (minPeriod - d - lpDelay_ < kMinCombLen) hi = mid;` — but lpDelay_ is assigned only at line 500 `lpDelay_ = -argH / w0;`, and prepare() (lines 195-220) never resets it.`

Why: The guard that decides how much dispersion to shed at the top of the keyboard sees the PREVIOUS note's one-pole phase delay (or 0.0 on the first note after construction), not this note's. lpDelay_ ranges ~0.4-0.67 samples across the keyboard at brightness 0.6, against a kMinCombLen of 4, so the shed decision and the bisected dispEff_ for a top-octave note depend on which note preceded it. That is the exact non-reproducibility class (per-note ring/dead chaos across octave 8) the 2026-08-13 guard was added to remove, and it breaks the seed-reproducibility rule since no seed is involved.

Recommendation: Move the lpDelay_ block (lines 495-501) above the shedding guard (line 480). Also reset lpDelay_ in prepare() so the first note after construction is not special. Add a pitch-accuracy test (DelayLine already has the measured-period pattern at engine_tests/main.cpp:322) over B7-B8 so ordering regressions are caught.


## F202 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/layered_red_noise_source.h:60

**LayeredRedNoiseSource::set_array leaves count_ inconsistent with the arrays and sync_arrays_ has a dead branch**

Reporters: sources-noise

Evidence: `lines 66-69: amplitudes_ = std::move(values); count_ = int(amplitudes_.size()); sync_arrays_();  — sync_arrays_ (113-118) then does `int n = int(frequencies_.size()); if (int(amplitudes_.size()) < n) amplitudes_.resize(n, 0.05f); else if (…> n) amplitudes_.resize(n);` so count_ no longer equals the layer count; line 117 `if (int(frequencies_.size()) < n) frequencies_.resize(n, 7.0f);` can never be true since n == frequencies_.size().`

Why: Result depends on JSON key order: amplitude array of 5 then frequency array of 3 yields count_=5 reported by get_setting("count") while rebuild_layers_ (line 122) builds 3 layers; the UI's count setting then shows a number that does not match the table. Also note the frequency path sets count_ before sync and the amplitude path sets it from the wrong array.

Recommendation: Make one function the canonical normaliser: after any set_array, n = max(frequencies_.size(), amplitudes_.size()) (or frequencies_.size() by contract), resize both to n with their descriptor defaults, then count_ = n. Delete the dead branch.


## F203 [UNVERIFIED] low (reporter: low) efficiency — engine/include/mforce/source/layered_red_noise_source.h:80

**LayeredRedNoise rebuilds its layers lazily in prepare() though everything needed is known at construction/edit time**

Reporters: sources-noise

Evidence: `lines 80-83: void prepare(...) { if (layersDirty_) rebuild_layers_(ctx); … }  with rebuild_layers_ (120-138) doing make_shared<RedNoiseSource> plus eight make_shared<ConstantSource> per layer (127-134), six of them for identical compile-time constants.`

Why: Verified the audio callback only pulls next() (main.cpp:3857-3927), so this is not an RT violation; but prepare() runs at every note-on (instrument.h:607) and the laziness exists only to defer work that set_setting/set_array (UI thread, edit time) could do immediately. The dirty flag and ctx plumbing are complexity with no payoff, and 8 allocations per layer for 6 shared constants is waste.

Recommendation: Rebuild eagerly in the constructor and in set_setting/set_array; delete layersDirty_. Hold std::vector<RedNoiseSource> by value (no shared ownership is needed) and share one ConstantSource per hardcoded constant.


## F204 [UNVERIFIED] low (reporter: low) duplication — engine/include/mforce/source/mesh2d_source.h:292

**tick0 and tick1 are mirror-image copies differing only in which plane set is read vs written**

Reporters: sources-physical

Evidence: `lines 292-318 `float tick0(...)` reads vxp_/vxm_/vyp_/vym_ and writes vxp1_/...; lines 320-346 `float tick1(...)` is the same 27 lines with the two plane sets swapped, including the duplicated edge_x/edge_y hook calls.`

Why: The edge-mode extension hook lives in both halves; any future change to scattering or boundary handling must be made twice and can drift. STK-verbatim is a stated design choice, but STK's layout can be kept while collapsing the code.

Recommendation: Group the four planes into a `struct Planes { float xp[N][N], xm[N][N], yp[N][N], ym[N][N]; }`, keep two instances, and write one `tick(const Planes& src, Planes& dst, int xo, int yo)` with `counter_ & 1 ? tick(p1_, p0_, ..) : tick(p0_, p1_, ..)`. Bit-identical; halves the hot-loop code.


## F205 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/multiplex_source.h:26

**reseed() per-note determinism contract not honored by MultiplexSource clones or DistortedSource**

Reporters: sources-combinators

Evidence: `multiplex_source.h has no `reseed()` override (struct spans 26-184); combined_source.h:307 `if (rng_.decide(d)) {` with no reseed override in DistortedSource (234-327).`

Why: dsp_value_source.h:122-127 defines reseed() as the in-line-note re-anchor and says RNG-bearing sources override it; fourteen sibling sources do (white/red/pink/wander noise, segment_source, envelope, noise_sources, layered_red_noise), and Vibrato forwards to its contained LFO (vibrato.h:144), establishing the container convention. deliver_continuation reseeds only vg.nodesById (instrument.h:475); the loader registers the mux itself there (patch_loader.cpp:1044 `valueNodes[id] = mux`) while the clone node maps live privately in instanceValueNodes_, so every stochastic node inside the N clones free-runs across in-line notes — exactly the 'different oboeist per phrase' drift the 09-20 addendum was written to stop. DistortedSource draws per sample and also never re-anchors. (StaticVar/StaticRange draw only in prepare, so their omission is moot.)

Recommendation: Add `void reseed() override { for (auto& m : instanceValueNodes_) for (auto& [_, n] : m) n->reseed(); }` to MultiplexSource and `void reseed() override { rng_.reanchor(); }` to DistortedSource.


## F206 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/source/multiplex_source.h:84

**templateJson_/baseSeed_ are write-only state and the header comment describes an access path that does not exist**

Reporters: sources-combinators

Evidence: `lines 84-85 `templateJson_ = std::move(json); baseSeed_ = baseSeed;` are the only writes; declarations at 170-171; repo-wide grep finds no read. Lines 19-20: 'external code updates templateJson_ to the current UI serialization' — the member is private with no accessor.`

Why: Dead members that copy the whole subtree JSON per node, and a comment that will send the next maintainer looking for a UI path that was never built. Also the redundant guard at line 122 `cur_ = (n > 0) ? sum / float(n) : 0.0f;` inside `if (!instances_.empty())`.

Recommendation: Drop templateJson_/baseSeed_ (the builder closure already captures both) and rewrite the comment to describe mark_dirty()+set_template as the actual rebuild path; remove the n>0 guard.


## F207 [UNVERIFIED] medium (reporter: medium) rt-safety — engine/include/mforce/source/multiplex_source.h:97

**set_clone_param allocates strings and a map node on every note-on, under the audio mutex**

Reporters: sources-combinators

Evidence: `lines 100-101: `pendingCloneParams_[nodeId + "." + paramName] = std::make_tuple(nodeId, paramName, value);`  line 164: `auto cs = std::dynamic_pointer_cast<ConstantSource>(cur);``

Why: Called per push binding on every note from instrument.h:344 (apply_note_bindings, via prepare_voice_at) and :493 (deliver_continuation, whose caller continue_voice_live is documented at :498-502 as acting on 'a pool voice the audio callback is pulling'). mforce_ui runs that path while holding g_audioMutex (main.cpp:4676), the same mutex fill_audio_buffer takes (main.cpp:3864), so each note-on does at least three heap allocations (temporary key string, two std::string copies into the tuple, map node on first use) plus an RTTI cast per clone while the callback is blocked. Not the per-sample loop, so not a rule violation, but it is avoidable work on the path that gates audio delivery. The key also duplicates the tuple's own nodeId/paramName.

Recommendation: Resolve each push binding once at load (or at rebuild_) into a per-clone vector of ConstantSource* and keep pending values as floats indexed by binding; set_clone_param then becomes a loop over raw pointers with no strings, and rebuild_ replays by re-resolving once. That also removes the duplicated key/tuple storage.


## F208 [UNVERIFIED] medium (reporter: medium) rt-safety — engine/include/mforce/source/multiplex_source.h:105

**MultiplexSource::prepare() can run a full JSON parse + build_graph for N clones; in the UI this happens under the audio mutex at note-on**

Reporters: arch-valuesource-graph

Evidence: `void prepare(const RenderContext& ctx, int frames) override { if (templateDirty_) rebuild_(); ...
rebuild_(): for (int i = 0; i < count_; ++i) { auto pair = builder_(i); ...

patch_loader.cpp:1143-1163 (what builder_ runs): json subtree = json::parse(subtreeJsonStr); ... auto g = build_graph(nodeMap, nodeOrder, sampleRate, perf);

tools/mforce_ui/main.cpp:4676-4692: std::lock_guard<std::mutex> lock(g_audioMutex); ... auto sv = pitched->prepare_voice_at(slot, noteNum, velocity, durationSeconds);   // -> vg.source->prepare()
tools/mforce_ui/main.cpp:3864 (audio_callback): std::lock_guard<std::mutex> lock(g_audioMutex);`

Why: templateDirty_ is true from set_template (load) until the first prepare, and again after set_setting("count") or mark_dirty(). The first note on each pool voice of a Multiplex patch therefore parses JSON and builds `count` (default 10, max 50) subgraphs through the registry while the UI thread holds g_audioMutex, which the audio callback blocks on → a guaranteed dropout proportional to count × subgraph size, repeated per voice. pin_model_design.md §4 accepts small note-on allocations as 'live-playback jitter', but a 10-50× graph build is a different order of magnitude, and the lazy trigger inside prepare() means the loader cannot even pre-build at load.

Recommendation: Build clones eagerly in the loader (call rebuild_ once right after set_template, off the audio path) and make prepare() only forward to existing instances. Treat `count` changes like any other structural edit: rebuild the instrument on the UI thread and swap the whole voice pool (the UI already has get_cached_instrument for exactly this), never inside prepare(). Add an assert/debug flag that prepare() never allocates when instances_ is non-empty.


## F209 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/noise_sources.h:16

**BlueNoiseSource has no reseed() override, so its embedded PinkNoiseSource is never re-anchored at a continuation note**

Reporters: sources-noise

Evidence: `noise_sources.h:16-59 defines BlueNoiseSource with member `PinkNoiseSource pink_;` (line 55) and no reseed(); the base default is a no-op (dsp_value_source.h:127). Every other RNG node in the unit overrides it (white 128, pink 88, red 67, wander 114/237, violet 105, velvet 175, perlin 279, crackle 340, murmuration 523, layered 86-88). instrument.h:475 `for (auto& [nid, src] : vg.nodesById) src->reseed();` dispatches per node, not per nested member.`

Why: Per-note determinism (the onsets-v2 addendum) silently fails for any patch using blue noise: the pink stream free-runs through a line while white/pink/red siblings re-anchor. Also a concrete cost of the inconsistent visibility: PinkNoiseSource::reseed is private (pink_noise_source.h:86-88) while RedNoiseSource's is public (red_noise_source.h:67), so the obvious fix `pink_.reseed()` will not compile until it is made public or called through a ValueSource&.

Recommendation: Add `void reseed() override { static_cast<ValueSource&>(pink_).reseed(); }` (or make PinkNoiseSource::reseed public) and make reseed() uniformly public across the unit so composite nodes can forward it.


## F210 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/source/noise_sources.h:169

**VelvetNoiseSource::recompute_gap ignores the density pin and hardcodes the descriptor default**

Reporters: sources-noise

Evidence: `lines 169-171: void recompute_gap() { nextImpulse_ = std::max(1, sampleRate_ / 2000); }  called from prepare() (148) after density_->prepare (146), so density_->current() is readable there.`

Why: The first impulse gap is always ~24 samples regardless of density; with density=20 (tools/gen_fm_matrix.py:278 uses that) the first impulse should arrive ~2400 samples in. Harmless after the first impulse (155-160 recompute from d) but the magic 2000 is a second copy of the descriptor default (128).

Recommendation: float d = density_->current(); nextImpulse_ = std::max(1, int(float(sampleRate_) / std::max(d, 1.0f))); and drop the literal.


## F211 [UNVERIFIED] high (reporter: high) soundness — engine/include/mforce/source/noise_sources.h:249

**PerlinNoiseSource narrows an unbounded double position to float before extracting the fraction, so the noise degrades over minutes**

Reporters: sources-noise

Evidence: `line 241: pos_ += double(spd) / double(sampleRate_);  line 249: total += amplitude * noise1d(float(pos_ * freq));  lines 263-264: int xi = int(std::floor(x)) & 255; float xf = x - std::floor(x);`

Why: pos_ grows without bound and is multiplied by freq up to lacunarity^7 before being cast to float. At the default speed 440 with 4 octaves (freq 8) x = 3520·t: after 60 s the float ulp is 2^-6 so xf has 64 levels on the top octave; after 10 min, 8 levels; at speed 20000 with 8 octaves it is a staircase within seconds. The fade/lerp output becomes stepped and eventually constant — a slow-onset corruption that a 5-second CLI render never shows but a live session does. Only pos mod 256 matters (xi & 255), so precision is being thrown away for nothing.

Recommendation: Do the integer/fraction split in double before narrowing: double xd = pos_ * freq; double fl = std::floor(xd); int xi = int(fl) & 255; float xf = float(xd - fl); and/or wrap pos_ at a multiple of 256 (exact for integer lacunarity). Add a test that the output statistics at t=0 and t=600 s match.


## F212 [UNVERIFIED] medium (reporter: medium) performance — engine/include/mforce/source/noise_sources.h:444

**MurmurationNoiseSource runs an O(n²) flock update plus a pow and a sin per bird at audio rate**

Reporters: sources-noise

Evidence: `lines 444-487 per-bird loop containing 456-467 `for (int j = 0; j < n; ++j) { … float absDist = std::fabs(dist); …}`; line 495 `float freq = centerFreq * std::pow(2.0f, b.freqOffset);`; line 502 `sum += std::sin(b.phase * 6.283185307f);` — all inside next().`

Why: At count=64 (descriptor max, line 377) that is 4032 inner iterations + 64 pow + 64 sin per sample ≈ 194 M iterations/s and 6 M transcendentals/s at 48 kHz for one node — the most expensive generator in the unit by two orders of magnitude, and count is a modulatable pin so a patch can hit it at any time. It is bounded and allocation-free, so not an RT-rule violation, but it will dominate a live voice's budget.

Recommendation: Update the flock at control rate (every 32-64 samples; fold the stride into dt) and keep only the phase accumulation per sample; replace std::pow(2,x) with std::exp2f; optionally sort offsets once per control tick so nearest-neighbour is O(n log n).


## F213 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/noise_sources.h:456

**Murmuration separation force sums every running-minimum neighbour, not the nearest one, so the result depends on bird index order**

Reporters: sources-noise

Evidence: `lines 460-466: if (absDist < minDist) { minDist = absDist; if (absDist < sep && absDist > 0.0001f) { sepForce += (dist / absDist) * sep * 0.5f / (absDist + 0.001f); } }  under the comment at 453 '// Separation: push away from nearest neighbor'.`

Why: sepForce accumulates a push for each j that happens to be the closest-so-far in index order and within `sep`, so bird 63 is repelled by up to 63 neighbours while bird 0 can be repelled by only those that improved the running minimum. The code does not implement what the comment says and the dynamics are index-order dependent — either a bug or an undocumented accident that the sound now depends on.

Recommendation: Track the index of the true nearest neighbour in the loop and apply the separation term once after it; if the current accidental behaviour is the sound Matt wants, rewrite the comment to describe it and remove the misleading minDist bookkeeping.


## F214 [UNVERIFIED] low (reporter: low) build — engine/include/mforce/source/phased_value_source.h:108

**Unused variable, unused member, and three unused includes in phased_value_source.h; shaper_source.h relies on a transitive <cmath>**

Reporters: sources-combinators

Evidence: `phased_value_source.h:108 `int olAdj = overlapSamples_ / (...);` never read; :3-4,9 include smoothness_interpolator.h, randomizer.h, <stdexcept> with no use; :63/:162 `totalFrames_` written, never read. shaper_source.h:1-6 has no <cmath> yet uses std::fabs/log2/pow (172, 233, 239).`

Why: C4189 warning noise on every TU that includes Phased; the includes pull mt19937 and interpolator code into TUs that do not need them; the Shaper include works only because curve.h happens to include <cmath>.

Recommendation: Delete olAdj, totalFrames_ and the three includes; add <cmath> to shaper_source.h.


## F215 [CONFIRMED-BY-MATT'S-CLAUDE] critical (reporter: critical) soundness — engine/include/mforce/source/phased_value_source.h:135

**Stage crossfade is run twice with a restart at every boundary, producing a discontinuity**

Reporters: sources-combinators

Evidence: `line 139: `float t = float(posInStage) / float(halfOl);`  line 145: `float t = float(halfOl - (stageLen - posInStage)) / float(halfOl);``

Why: Traced: on the last sample of stage i the transToNext branch has t=(halfOl-1)/halfOl so cur_ is almost entirely stage i+1; on the very next sample (stageIdx=i+1, posInStage=0) the transFromPrev branch has t=0 so cur_ is entirely stage i again, then fades to i+1 a second time. The output jumps back to the previous stage's signal at every boundary (a click) and the transition takes two half-overlap fades instead of one. The legacy source this was ported from drives both halves from ONE CombinedSource(Fade) whose counter runs continuously across the boundary (PhasedValueSource.cs:67-80 use Transitions[index-1]/Transitions[index], built at :152-161 with count=OverlapCount), so this is a port divergence, not legacy behavior. patches/baselines/phased_test.json (overlap 0.15, three stages) exercises exactly this path. Secondary divergence verified: line 95 folds the overlap allowance into stageCounts_[expandIdx] and lines 109-110 accumulate it into boundaries_, whereas legacy excludes it from the boundary (PhasedValueSource.cs:44 `- OverlapCount / (...)`), so a middle expand stage pushes every later boundary late by overlapSamples_.

Recommendation: Parametrize t over the whole overlap window: transToNext t = (halfOl - (stageLen - posInStage)) / (2*halfOl); transFromPrev t = (halfOl + posInStage) / (2*halfOl), so the two zones are the two halves of one monotonic fade. Exclude the overlap allowance from boundaries_ (compute boundaries from the un-padded stage lengths). Re-render phased_test.json and diff against the pre-fix WAV to confirm the boundary clicks disappear.


## F216 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/source/pulse_source.h:54

**Dead assignments, unused include, conversational TODO comment and never-null checks left in the unit**

Reporters: sources-noise

Evidence: `pulse_source.h:54-59: float duty = dutyCycle_->next(); float bend = bend_->next(); // Use current() after next() … duty = dutyCycle_->current(); bend = bend_->current();  (first assignments dead). red_noise_source.cpp:4 `#include <iostream>` (nothing streams). red_noise_source.cpp:96-98 '// If you want perfect equivalence, we can add LastVal tracking to SingleValueSource layer.' layered_red_noise_source.h:120 `rebuild_layers_(const RenderContext& /*ctx*/)` unused param; lines 82, 87, 92 `if (l)` on layers that are always make_shared'd (126, 135).`

Why: Each is small, but together they read as unfinished port scaffolding in files that are otherwise release-shaped; the pulse one in particular invites a future reader to 'fix' the double read and change timing. The red-noise comment is a chat transcript, not documentation, and leaves a known inexactness (continuity uses lastValue_ not the previous output sample) unresolved.

Recommendation: Collapse pulse to next(); current() once; remove <iostream>; turn the continuity note into a one-line 'differs from legacy: …' statement or fix it; drop the unused param and null checks.


## F217 [UNVERIFIED] low (reporter: low) testing — engine/include/mforce/source/repeating_source.h:80

**No engine test exercises any node in this unit**

Reporters: sources-evolution

Evidence: `grep of tools/engine_tests/main.cpp for RepeatingSource|SegmentSource|WavetableSource|Evolution: 0 matches.`

Why: The gap-rounds-to-zero silence (finding 1), the empty-target divide-by-zero, and the mid-note rebuild default fallback would all be caught by a few dozen lines of deterministic-seed render asserts. hi_hat1 and the three ep_* / piano_seg library patches depend on this unit.

Recommendation: Add: RepeatingSource with gapDuration=0 emits >1 repetition; WavetableSource+each holder renders 2 notes without allocation (instrument a counting allocator) and reproduces byte-identically from seed; TargetEvolution rejects an empty target.


## F218 [UNVERIFIED] high (reporter: high) soundness — engine/include/mforce/source/repeating_source.h:81

**RepeatingSource never re-triggers when the gap rounds to zero samples — permanent silence after the first repetition**

Reporters: sources-evolution

Evidence: `if (gapCount_ == 0) { if (srcRemaining_ > 0) {...} else { gapCount_ = int(std::round(currGap_ * float(sampleRate))); cur_ = 0.0f; } } else { gapCount_--; if (gapCount_ == 0) { prepare_repetition(); } ... }`

Why: prepare_repetition() is only reached from the in-gap branch, which requires gapCount_ != 0. With gapDuration < 0.5/sampleRate (the setting descriptor at line 53 allows 0.0, and gapVarPct can drive currGap_ negative → clamped to 0 at line 109), the source-finished branch sets gapCount_ = 0 and every subsequent next() re-enters that same branch forever. A permitted setting value produces silence with no error. Minor sibling: with gap > 0 the gap is actually gapSamples + 2 (the transition sample at 88-89 and the re-prepare sample at 94-97 both emit 0).

Recommendation: Replace the gapCount_==0 sentinel with an explicit state (Playing/Gap) and, when the source finishes, call prepare_repetition() immediately if the computed gap is 0; or compute gapCount_ = max(1, ...) and treat the re-prepare sample as the first source sample.


## F219 [UNVERIFIED] high (reporter: high) rt-safety — engine/include/mforce/source/repeating_source.h:95

**RepeatingSource::next() re-runs prepare() on its subgraph, pulling unbounded setup work (JSON graph rebuilds, node allocation, RTTI walks) into the sample path**

Reporters: arch-modern-cpp

Evidence: `95: `prepare_repetition();` inside next(); 112: `if (source_) source_->prepare(ctx_, samples);`. Subgraph prepare() is not bounded: multiplex_source.h:106 `if (templateDirty_) rebuild_();` (134-151 constructs count_ clone graphs via std::function builder_), vibrato.h:98 `build_lfo();` (121 allocates the LFO node chain), delay_line_source.h:154 `if (auto* rs = dynamic_cast<RefSource*>(n))` walk; delay_line_source.h:90 even states "prepare() may run at note-on in the audio callback"`

Why: prepare() is the one place the engine allows allocation; calling it from next() makes every node's prepare cost a per-sample cost at repetition boundaries, and does so on the audio thread in the UI.

Recommendation: Give ValueSource a cheap `reset()`/`retrigger()` (state-only, no allocation) and have RepeatingSource call that; keep prepare() strictly for load/setup. MultiplexSource must rebuild from set_setting/set_array, never lazily in prepare().


## F220 [UNVERIFIED] medium (reporter: medium) rt-safety — engine/include/mforce/source/repeating_source.h:112

**RepeatingSource re-runs the child graph's prepare() from inside next() on every repetition**

Reporters: sources-evolution

Evidence: `void prepare_repetition() { ... if (source_) source_->prepare(ctx_, samples); srcRemaining_ = samples; gapCount_ = 0; }   // called from next() at line 95`

Why: prepare() is the engine's note-setup hook and the one place where allocation and layout draws are sanctioned (dsp_value_source.h:122-126 even says prepare 'consumes layout draws'). Invoking it per repetition from the per-sample path turns whatever the child subgraph legitimately does in prepare into audio-thread work, and re-draws the child's layout each repetition. It also means the child's semantics differ from every other node's (prepared once per note).

Recommendation: Either document RepeatingSource as offline-only, or give children a cheap `restart()` hook (reset phase/ptr, no allocation) that RepeatingSource calls instead of prepare().


## F221 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/saw_source.h:18

**PolyBLEP residual is duplicated between SawSource and PulseSource**

Reporters: sources-noise

Evidence: `saw_source.h:19-25: if (currPos_ < currPhaseIncr_) { float t = currPos_ / currPhaseIncr_; blep = t + t - t * t - 1.0f; } else if (currPos_ > 1.0f - currPhaseIncr_) { … blep = t * t + t + t + 1.0f; }  — pulse_source.h:72-81 get_blep(pos, phaseIncr) is the identical function (grep for the expression finds exactly these two sites).`

Why: Two copies of a numerically sensitive kernel; a future upgrade (e.g. 4-point BLEP, or a BLAMP for the triangle) would be applied to one and missed in the other.

Recommendation: Move get_blep as a free inline function (poly_blep(float pos, float incr)) into dsp_wave_source.h next to the phase accumulator it depends on, and have both oscillators call it.


## F222 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/saw_source.h:18

**Oscillator / filter math duplicated across nodes: BLEP, Friedlander friction, resonance and RBJ biquad coefficients, random-walk variation**

Reporters: arch-duplication

Evidence: `saw_source.h:18-25 vs pulse_source.h:72-81 identical polynomial BLEP (`blep = t + t - t * t - 1.0f;` / `t * t + t + t + 1.0f`); Friedlander `std::pow(std::fabs(gain*vRel)+0.75f, -4.0f)` clamp 0.98 at wave_evolution.h:464-468, ks_string.h:308-311, bow_table_source.h:80-82; `a1 = -2 r cos(w); a2 = r*r` at biquad_source.h:104-106 vs mesh2d_source.h:225-226; RBJ coefficients full_additive_source.cpp:28-38 vs wave_evolution.h:529-543; additive random-walk variation basic_additive_source.h:148-160 vs additive_source2.cpp:148-164 (+init 127-132 vs 119-124); `lerp` noise_sources.h:274 vs smoothness_interpolator.h:12.`

Why: Each is a 5-15 line numerical kernel whose correctness was tuned once (e.g. the Friedlander clamp) and then copied; the copies cannot be fixed or retuned in one place.

Recommendation: `blep()` in dsp_wave_source.h; `friedlander(vRel, gain)` in bow_table_source.h used by ks_string/wave_evolution; `rbj_lowpass()/resonator_coefs()` in filter/biquad_source.h; one `lerp` in fast_math.h; a shared `RandomWalkParam` for the two additive sources.


## F223 [UNVERIFIED] medium (reporter: medium) smell — engine/include/mforce/source/segment_source.h:22

**SegmentSource and RepeatingSource carry a constructor-time public `sampleRate` and ignore RenderContext — two sources of truth after the render-context refactor**

Reporters: sources-evolution

Evidence: `int sampleRate{48000};  ... prepare(const RenderContext& ctx, int frames) never reads ctx.sampleRate (146-161); conversions at 214/221 use the member.  RepeatingSource: `ctx_ = ctx;` (76) yet `std::round(currGap_ * float(sampleRate))` (88) and `d * float(sampleRate)` (111).`

Why: The 2026-04-20 render-context plan made ctx.sampleRate the propagated rate ('instead of hardcoded or cached-stale values', plan line 220). These two nodes keep a second, mutable, public copy set at registry time (source_registrations.cpp:374-381); RepeatingSource even stores the ctx and then does not use its rate. Any future non-48k render silently uses the stale constructor value for seconds→samples.

Recommendation: Capture `sampleRate_ = ctx.sampleRate` in prepare(), drop the ctor argument and the public field; RepeatingSource should convert with ctx_.sampleRate.


## F224 [UNVERIFIED] low (reporter: low) soundness — engine/include/mforce/source/segment_source.h:164

**SegmentSource null-checks only width_ in next() while set_param accepts nullptr for every pin; constructor initialiser order differs from declaration order**

Reporters: sources-evolution

Evidence: `amplitude_->next(); smoothness_->next(); if (width_) width_->next(); gap_->next(); gapVarPct_->next(); ...   /   ctor (134): `: values_(...), sampleRate(sr), oneShot(os), rng_(seed), amplitude_(...)` vs declaration order oneShot, sampleRate (21-22) ... amplitude_ ... values_ (273-280) ... rng_ (290)`

Why: set_param (57-65) stores whatever shared_ptr it is given; a disconnect that passes nullptr dereferences on the next sample for six of the seven pins but not the seventh — inconsistent and a latent crash. The initialiser order mismatch is harmless today (no cross-member dependence) but is a -Wreorder / C5038 warning and a trap for the next edit.

Recommendation: Either reject nullptr in set_param (restore the ConstantSource default) or check all pins uniformly; reorder the initialiser list.


## F225 [UNVERIFIED] low (reporter: low) modern-cpp — engine/include/mforce/source/sine_source.h:15

**Hand-rolled constants and bit loops where C++20 <numbers>/<bit>/<cmath> provide the idiom**

Reporters: sources-noise

Evidence: `sine_source.h:15 `std::sin(currPos_ * 2.0f * 3.14159265358979323846f)`; noise_sources.h:502 `6.283185307f`; pink_noise_source.h:63-67 `while ((n & 1) == 0) { n >>= 1; numZeros++; }`; noise_sources.h:495 `std::pow(2.0f, b.freqOffset)`; noise_sources.h:281 `int perm_[512];` holding values 0..255.`

Why: Two different pi literals of different precision across the unit; the trailing-zero loop is a single std::countr_zero; pow(2,x) is slower and less accurate than exp2; perm_ costs 2 KB per PerlinNoiseSource for 512 bytes of information (matters for Multiplex clones).

Recommendation: std::numbers::pi_v<float> (one constant in a shared header), std::countr_zero(unsigned(index_)), std::exp2f, std::array<uint8_t, 512> perm_.


## F226 [UNVERIFIED] low (reporter: low) modern-cpp — engine/include/mforce/source/sine_source.h:15

**Pi written out as a literal in ~25 places with differing precision**

Reporters: arch-duplication

Evidence: `sine_source.h:15, ramp.h:47, smoothness_interpolator.h:16, envelope.h:522, noise_sources.h:502, hybrid_ks_source.cpp:7, basic_additive_source.h:135, additive_source2.cpp:144, filters.h:63/67/94/98, biquad_source.h:104/130, svf_source.h:106/119/155, ks_string.h:462/577/578, allpass_resonator.h:189/224, hammer_bank.h:128, mesh2d_source.h:225, full_additive_source.cpp:31, mixer.cpp:41-42, fft.h:22, signal_stats.h:58, wave_evolution.h:530 `3.1415926536f`, patch_loader.cpp:961, drift_voicing_profile_selector.h:76, mforce_ui/main.cpp:3966 and 13383, stk_ref/mesh2d_ref.cpp burst_sample.`

Why: Cosmetic, but the differing precisions (3.1415926536f vs 3.14159265358979323846f) are exactly the kind of thing that breaks bit-exact gates (the mesh2d harness deliberately replicates float ops "op for op").

Recommendation: `std::numbers::pi_v<float>` (C++20) or one `kPi`/`kTwoPi` in core/fast_math.h.


## F227 [UNVERIFIED] low (reporter: low) workaround-hack — engine/include/mforce/source/triangle_source.h:45

**`asymmetric` setting keeps an earlier per-leg warp alive as a toggle**

Reporters: sources-noise

Evidence: `lines 47-49: // Off: symmetric legs … On: the earlier per-leg warp, which bends the two sides oppositely ("shark fin"). {"asymmetric", SettingType::Bool, …}  and line 111-114 branch in compute_wave_value.`

Why: Header line 19-22 frames it as 'restores the earlier per-leg warp' — i.e. a compat path for a superseded behaviour, which the project's no-compat-toggles rule says should just be replaced. If the shark-fin is a wanted timbre it is a feature and the comment should say so; if not, it is a branch and a setting carried for history.

Recommendation: Decide: either document it as an intentional second shape (and drop the 'earlier/restores' framing), or remove the flag and the branch.


## F228 [UNVERIFIED] high (reporter: high) soundness — engine/include/mforce/source/triangle_source.h:99

**TriangleSource emits NaN when bias is 0, a value its own descriptor permits**

Reporters: sources-noise

Evidence: `line 39: {"bias", 0.5f, 0.0f, 1.0f, "0-1"}  …  line 98-99: if (currPos_ <= b) { return -1.0f + currPos_ * (4.0f / b) / 2.0f;  …  line 107: float t = std::clamp(currPos_ / b, 0.0f, 1.0f);`

Why: On the first sample currPos_ = phase = 0 (dsp_wave_source.h:41), so with b == 0 the neutral branch computes 0 * inf = NaN and the warped branch computes 0/0 = NaN (std::clamp of NaN stays NaN). WaveSource::next multiplies it into cur_ and it reaches the mixer; any downstream IIR (filters, delay lines) latches NaN permanently. The UI slider and CLI JSON both allow exactly 0.0. Only RefSource taps scrub non-finite values, not ordinary pins.

Recommendation: Clamp the bias once per sample to [eps, 1-eps] (e.g. 1e-4f) before the two divisions, or raise the descriptor minimum to a non-zero value and clamp defensively anyway; add a unit test rendering bias=0 and bias=1 for one cycle and asserting std::isfinite on every sample.


## F229 [UNVERIFIED] high (reporter: high) soundness — engine/include/mforce/source/wander_noise_source.h:66

**WanderNoiseSource and WanderNoise2Source expose an `amplitude` pin that is prepared but never advanced or applied**

Reporters: sources-noise

Evidence: `line 36: {"amplitude",  1.0f,  0.0f, 10.0f},  line 60: if (amplitude_)  amplitude_->prepare(ctx, frames);  lines 67-69 advance only speed_/deltaSpeed_/slopeLimit_;  line 100: cur_ = localVal * 2.0f - 1.0f;  (WanderNoise2: lines 153, 183, 192-196, 222 same shape). WanderNoise3Source DOES apply it: line 306 amplitude_->next(); line 343 cur_ = (value_ * 2.0f - 1.0f) * amp;`

Why: The descriptor, the UI pin and the config-pin census all advertise a working amplitude input. patches/baselines/wander1_test.json:16 wires "amplitude": {"ref": "env"} expecting an envelope — that Envelope is prepared but never next()'d and never multiplied in, so the baseline renders at constant level. Traced to legacy: WanderNoiseSource.cs:66 `Value = LocalVal * 2 - 1;` has the same omission, so the port faithfully reproduced a legacy bug.

Recommendation: Advance and apply the pin exactly as WanderNoise3Source does: amplitude_->next(); cur_ = (localVal*2-1) * amplitude_->current(); (same for WanderNoise2 on value_). Per the no-patch-back-compat rule just change it and accept that wander1_test.json / wander2_mod_test.json baselines move; note the change in the commit.


## F230 [UNVERIFIED] low (reporter: low) modern-cpp — engine/include/mforce/source/wave_evolution.h:24

**evolve()/shape_excitation() take std::vector<float>& — exposing the container lets an evolution reallocate the ring buffer in the render path; pi is a hand-typed literal**

Reporters: sources-evolution

Evidence: `virtual void evolve(std::vector<float>& values, int index) = 0;  ... EKS: `values = std::move(filtered);` (290)  ... Brass: `2.0f * 3.1415926536f * cutoff` (530)`

Why: `std::span<float>` (C++20, already used by the descriptors) would make resize/replace impossible by type, which is exactly the guarantee the no-allocation rule wants, and would let WavetableSource own a fixed-capacity buffer. `std::numbers::pi_v<float>` is the C++20 constant.

Recommendation: Change both virtuals to `std::span<float>`; EKS filters in place via a member scratch; use std::numbers::pi_v<float>.


## F231 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/wave_evolution.h:45

**Parked.txt item 3 (48k rate-bake) is CONFIRMED by the code and is broader than the note states; the note is also stale (names a deleted class)**

Reporters: sources-evolution

Evidence: `sampleCount_ = float(std::round(std::max(24.0 - 4.0 * logF * logF, 2.0))) + muting_ * 20.0f; ... speed_ = float(std::max(9.0 - std::pow(logF, 2.0), 1.0)) + muting_ * 4.0f;`

Why: Pluck (45-53) sizes its averaging window and per-sample speed in samples without SR; Averaging (118-120) likewise; EKS pick-direction one-pole (296-303) and stiffness allpass (326) are per-sample coefficients; Reed loopFilter/tubeLoss (373-374) per sample. The note says BowedString and Brass are fixed, but only their delay lengths (426-431) and biquad (529-543) are SR-aware — their tubeLoss/brightness (475-481) and lipTension/tubeLoss (561-562) are still per-sample constants, so loss-per-period and loop cutoff shift with SR. Every algorithmic evolution is per-sample by construction (dt 1011, swapsPerSample 1124, stepEvery 1197/1483, rate 1298/1387, morphRate 176). Parked.txt names BlownTubeEvolution, which no longer exists anywhere outside docs. BACKLOG 69 explicitly defers this while everything renders at 48k, so the cost is latent, not current.

Recommendation: Decide per setting whether it is 'per period' or 'per second' and convert in on_prepare(ctx) (the hook exists precisely for this); until then, assert ctx.sampleRate == 48000 in WaveEvolution::on_prepare so the drift cannot surprise anyone, and correct the Parked.txt entry.


## F232 [UNVERIFIED] low (reporter: low) smell — engine/include/mforce/source/wave_evolution.h:56

**adjust() returns a frequency correction nobody consumes; dead members in Target and EKS**

Reporters: sources-evolution

Evidence: `freqAdj_ = -(sampleCount_ / 24.0f) * float(logF - 2.0) * 0.15f; return freqAdj_;   — both call sites discard it: `evolution_->adjust(currFreq_);` (wavetable_source.cpp:48, 103)`

Why: The tuning allpass superseded the legacy freqAdj (EKS comment at 272 says so), so PluckEvolution computes and stores a value that is never read, and the interface advertises a contract the holder does not honour. TargetEvolution's `Randomizer rng_` (235) is seeded and never used (2.5 KB of mt19937 state per instance); EKS `freq_` (337) is written at 259 and read only in the same function (265).

Recommendation: Change adjust() to `void`, delete freqAdj_, rng_ in Target, and freq_ in EKS.


## F233 [UNVERIFIED] high (reporter: high) rt-safety — engine/include/mforce/source/wave_evolution.h:284

**Every evolution (and WavetableSource itself) allocates its scratch on the first next() of a note instead of in prepare(); EKS allocates and frees a full table every note**

Reporters: sources-evolution

Evidence: `std::vector<float> filtered(len); for (...) filtered[i] = values[i] - values[di]; values = std::move(filtered);  // EKS shape_excitation, called from compute_raw at ptr_==0`

Why: fill_table (wavetable_source.cpp:27 values_.resize(len)), EKS (above — the move-assign replaces WavetableSource's buffer with an exact-size one, so capacity is never retained and this is an alloc+free on EVERY note), BowedString adjust (433-434 bridgeLine_.assign / neckLine_.assign), Brass shape_excitation (551 rawLoop_.assign), ReactionDiffusion shape_excitation (1018-1021, four assigns), CellularAutomaton (1206-1207 and 1216-1217), Target (195 resize), Bezier (1407 resize), HistogramEqualize adjust (1301 cdf_.assign). All run on the audio thread at note-on. assign/resize are amortised once a reused voice has played its lowest note, but fresh voices and any lower note reallocate, and EKS never amortises. The project rule is explicit: no heap allocation in render paths.

Recommendation: Reserve once: WavetableSource::prepare reserves sampleRate/minFreq (e.g. 48000/20 = 2400) for values_; add the same reservation to WaveEvolution::on_prepare(ctx) for each evolution's scratch (they already receive ctx). EKS: run the comb filter in place via a member scratch buffer, never replace `values`.


## F234 [UNVERIFIED] low (reporter: low) efficiency — engine/include/mforce/source/wave_evolution.h:466

**Per-sample std::pow in the bow friction table and up to ~570 modulo divisions per sample in PluckEvolution**

Reporters: sources-evolution

Evidence: `float fricCoef = std::pow(bt, -4.0f);   /   for (int i = 0; i < loops; ++i) average(values, (index + i) % len);  →  for (int i = 0; i < sampCount; ++i) sum += values[(index + i) % len];`

Why: pow(x, -4) is a libm call per sample where `1/((bt*bt)*(bt*bt))` is three multiplies and a divide. Pluck's nested loop (64-66, 80-82) runs loops (speed_, up to ~13) × sampCount (up to 24+20) iterations, each with an integer `%` — a ring index with a branch wrap removes the division. Legacy-faithful, but this is the hottest per-sample path in the unit.

Recommendation: Replace pow with the reciprocal product; in average(), compute a wrapped start index once and increment with `if (++j == len) j = 0`.


## F235 [UNVERIFIED] high (reporter: high) duplication — engine/include/mforce/source/wave_evolution.h:603

**Thirteen *EvolutionSource holder classes are ~580 lines of copy-pasted boilerplate; wave_evolution.h is a 1552-line god-file that should be split**

Reporters: sources-evolution

Evidence: `WaveEvolution* get_evolution() override { return &evo_; } ... void set_setting(std::string_view name, float value) override { if (name == "tubeLoss") evo_.tubeLoss = value; else if (name == "loopFilter") evo_.loopFilter = value; } float get_setting(...) { if (name == "tubeLoss") return evo_.tubeLoss; ... } void prepare(...) override {} float next() override { return 0.0f; } float current() const override { return 0.0f; }`

Why: Every holder (PluckEvolutionSource 603, Averaging 642, EKS 701, Reed 749, BowedString 826, Brass 909, RD 1070, SortErosion 1150, CA 1245, HistEq 1341, Bezier 1420, BitRotate 1502) repeats the identical shape: get_evolution, no-op prepare/next/current, and an if-chain set_setting/get_setting that must be kept in sync by hand with setting_descriptors — three places per setting, ~45 settings. The four pin-holding ones (775-783, 852-860, 935-943, 1523-1531) also repeat the shared_ptr+raw-pointer sync verbatim. Adding a model today means copying 45 lines. The single header is included by wavetable_source.h and therefore by every TU that touches WavetableSource (including the 13k-line mforce_ui/main.cpp), so all 26 classes recompile everywhere.

Recommendation: One `template <class Evo> struct EvolutionHolder final : ValueSource, IEvolutionHolder` driven by a constexpr table in each Evo: `{name, float Evo::*member, SettingType, def, min, max}` for settings and `{name, ValueSource* Evo::*pin, shared_ptr slot}` for pins; descriptors/set/get derive from the table. Then split: wave_evolution.h (interface + holder template), evolution/pluck.h, evolution/eks.h, evolution/reed_brass_bowed.h, evolution/algorithmic.h — or one header per model.


## F236 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/wave_evolution.h:603

**Twelve WaveEvolution holder wrappers repeat the same ValueSource shell**

Reporters: arch-duplication

Evidence: `PluckEvolutionSource 603-636 through BitRotateEvolutionSource 1502-1550: each declares `get_evolution()`, a no-op `prepare`, `float next() override { return 0.0f; }`, and the same setting_descriptors/set_setting/get_setting forwarding.`

Why: ~30 lines × 12 of boilerplate; every new evolution type adds another copy, and the no-op next() shell is exactly the kind of thing that drifts (one wrapper gains a seed or a setting the others lack).

Recommendation: `template <class Evo> struct EvolutionHolder : ValueSource` with the type name supplied by the Evo class; register instances in source_registrations.cpp.


## F237 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/wave_evolution.h:620

**Live setting edits on Pluck/Averaging holders replace the evolution object and lose adjust() state until the next note — the sounding note does not reflect the slider**

Reporters: sources-evolution

Evidence: `if (name == "muting") { muting_ = value; evo_ = PluckEvolution(muting_, seed_); }   /  void rebuild() { evo_ = AveragingEvolution(sampleCount_, speed_, decayFactor_, leading_, autoAdjust_, seed_); }`

Why: A fresh AveragingEvolution has sampleCount_{2.0f} and decayFactor_{1.0f} (159-160); a fresh PluckEvolution has sampleCount_{2.0f}, speed_{1.0f} (87-88). Those are only derived in adjust() (104-125, 42-58), which WavetableSource calls solely at ptr_==0 (wavetable_source.cpp:48). The UI applies set_setting live under g_audioMutex (tools/mforce_ui/main.cpp:9409-9410), so dragging decayFactor while a note sounds sets the note's decay to 1.0 (no decay) and its window to 2 — not the slider value — until retrigger, and the RNG stream restarts. This contradicts the 'tweak parameter, hear the results' mission item. The other holders (EKS 721-727, Reed 799-802, Brass, Bowed) write fields directly and do not have this problem.

Recommendation: Write settings into base fields on the live object (as the other holders do) and have adjust() derive runtime state from base values; never copy-assign the evolution.


## F238 [CONFIRMED-BY-MATT'S-CLAUDE] high (reporter: high) rt-safety — engine/include/mforce/source/wave_evolution.h:1326

**HistogramEqualizeEvolution allocates a fresh std::vector<int> once per table cycle on the audio thread**

Reporters: arch-build-headers, sources-evolution

Evidence: `void rebuild_cdf(const std::vector<float>& values) { if (int(cdf_.size()) != bins) cdf_.assign(bins, 0.0f); std::vector<int> counts(bins, 0); for (float v : values) counts[sample_to_bin(v)]++;`

Why: rebuild_cdf is called from evolve() at index==0 (line 1310), i.e. once per period — at 1 kHz that is 1000 heap allocations plus frees per second inside WavetableSource::next(), which in mforce_ui runs inside audio_callback (tools/mforce_ui/main.cpp:3890). Direct violation of the project's no-heap-in-render-loops rule; the evolution also does an O(len) pass per cycle, which is acceptable (amortised O(1)/sample) but the allocation is not.

Recommendation: Make counts_ a member sized in adjust()/on_prepare (bins is a setting, so size once when it changes) and std::fill it per cycle.

Also reported as: Heap allocation inside the per-sample render path: HistogramEqualizeEvolution::evolve allocates a vector every table cycle; three other evolutions allocate lazily from evolve()


## F239 [UNVERIFIED] high (reporter: high) rt-safety — engine/include/mforce/source/wave_evolution.h:1328

**WaveEvolution::evolve() implementations allocate per table cycle (HistogramEqualize, TargetEvolution, BezierPull, CellularAutomaton)**

Reporters: arch-modern-cpp

Evidence: `1328: `std::vector<int> counts(bins, 0);` in rebuild_cdf(), called from evolve at 1310 `if (index == 0) rebuild_cdf(values);` i.e. every cycle; 195: `resampledTarget_.resize(len);` (TargetEvolution::evolve 189); 1407: `target_.resize(len);` (BezierPull evolve 1398); 1216-1217: `state_.assign(len, 0); snapshot_.assign(len, 0);` (CellularAutomaton evolve 1211)`

Why: evolve() is invoked per sample from WavetableSource::compute_raw (wavetable_source.cpp:69 `evolution_->evolve(values_, tablePtr2_)` and 127); a fresh std::vector<int> per fundamental period is a steady-state allocation on the audio thread, not a one-off.

Recommendation: Make counts a member sized in prepare() (std::vector<int> counts_ reused with std::fill), and move all len-dependent resizes to a `void prepare(int len)` hook on WaveEvolution called by WavetableSource::prepare.


## F240 [UNVERIFIED] medium (reporter: medium) soundness — engine/include/mforce/source/wavetable_source.h:15

**Nothing in this unit implements reseed(); WavetableSource's internal noise and every evolution Randomizer free-run through an in-line note**

Reporters: sources-evolution

Evidence: `WavetableSource declares no reseed() override; its excitation source is `inputSource_(std::make_shared<WhiteNoiseSource>(inputSeed))` (wavetable_source.cpp:10) — a private object, not a graph node.`

Why: dsp_value_source.h:122-127 defines the onsets-v2 per-note-determinism contract ('RNG-bearing sources override'); instrument.h:475 reseeds every node in nodesById. SegmentSource honours it (segment_source.h:289). WavetableSource does not, and its WhiteNoiseSource is unreachable from nodesById, so the KS excitation is never re-anchored; the holders for Pluck (Randomizer at 90), Averaging (161), EKS (336), Reed (385) and Brass (581) have no reseed() either, so per-sample draws (floorOrCeiling windows, drum-blend flips, breath noise) drift exactly the way the 'different oboeist per phrase' symptom described. hi_hat1.json in patches/library uses EKS+WavetableSource.

Recommendation: Add `virtual void reseed() {}` to WaveEvolution; each RNG-bearing evolution calls rng_.reanchor(); every holder forwards; WavetableSource::reseed forwards to inputSource_ and evolution_.


## F241 [UNVERIFIED] medium (reporter: medium) workaround-hack — engine/include/mforce/source/wavetable_source.h:21

**Two ownership/wiring paths for the evolution (legacy string-typed unique_ptr vs holder-node shared_ptr) with divergent defaults; TargetEvolution is reachable only via the legacy path and divides by zero on an empty target**

Reporters: sources-evolution

Evidence: `void set_evolution(std::unique_ptr<WaveEvolution> evo) { ownedEvolution_ = std::move(evo); evolution_ = ownedEvolution_.get(); }  ... set_param("evolution"): auto* holder = dynamic_cast<IEvolutionHolder*>(evolutionSrc_.get()); evolution_ = holder ? holder->get_evolution() : nullptr;`

Why: patch_loader.cpp:945-975 constructs Pluck/Averaging/Target from JSON strings with its own defaults (sampleCount 2.0, decayFactor 0.996 at 950-951) that disagree with the holder nodes' defaults (4.0 / 0.999 at wave_evolution.h:644, 653-655), so the same evolution sounds different depending on which patch form wrote it. CLAUDE.md says no patch back-compat, so the string path is dead weight. TargetEvolution (175) has no *Source holder and is absent from source_registrations.cpp, so it is unreachable from the UI graph; the loader lets `target` stay empty when neither targetWave nor targetPartials is present (patch_loader.cpp:954-970), and evolve() then executes `int(pos) % int(target_.size())` (199-200) — integer division by zero on the first sample. A null set_param("evolution") also leaves ownedEvolution_ alive while evolution_ becomes nullptr (two owners, one raw pointer).

Recommendation: Delete set_evolution/ownedEvolution_ and the loader's string path; give TargetEvolution a holder (so it is a first-class node) or delete it; reject an empty target at construction.


## F242 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/white_noise_source.h:40

**Every class hand-maintains five parallel pin lists plus dead typed accessors; the drift bugs above are the measured cost**

Reporters: sources-noise

Evidence: `white_noise_source.h: descriptor table 40-49, set_param chain 51-57, get_param chain 58-65, prepare list 67-73, next list 76-80 — five copies of the same five names. Dead typed accessors with no caller outside their own header (grep-verified): red_noise_source.h:12-24, wander_noise_source.h:21-29, 134-146, 256-263, pulse_source.h:18-19, triangle_source.h:29-30.`

Why: Sixteen classes × five lists = ~80 hand-synchronised name lists in this unit alone. The RedNoise/WanderNoise2/LayeredRedNoise default drift and the Wander amplitude pin that is in four lists but not the fifth are direct products of this pattern. The set_X/get_X pairs are a sixth list that nothing calls (the only set_power/get_power hits in main.cpp are Formant's). This is boilerplate, not a registry, so it is not covered by the explicit-registries rule.

Recommendation: Per class, one static constexpr table of {ParamDescriptor, std::shared_ptr<ValueSource> Self::* member}; a CRTP or base helper implements param_descriptors/set_param/get_param/prepare/advance from it, and the constructor initialises each member from desc.default_value. Delete the typed set_X/get_X pairs. This is a mechanical refactor that removes ~300 lines from this unit and makes the drift class of bug unrepresentable.


## F243 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/white_noise_source.h:98

**Sign/boost/continuity draw block is copied verbatim between WhiteNoiseSource and RedNoiseSource**

Reporters: sources-noise

Evidence: `white_noise_source.h:101-113: if (rng_.decide(zct)) { lastSign_ = -lastSign_; } else { lastSign_ = float(rng_.sign()); if (lastSign_ == 0.0f) lastSign_ = 1.0f; } … v = rng_.range(b, 1.0f) * lastSign_; if (cont != 0.0f) { const float influence = std::min(cont, 0.999f); v = rng_.range(lastVal_, v, lastVal_, influence); }  — red_noise_source.cpp:80-100 is the same 20 lines with nextValue_/lastValue_ renamed.`

Why: Both copies carry the same subtle details (the sign==0 → 1 edge fix, the 0.999 continuity cap, the lastSign_ = -1 'get going' initial). The white copy was written by reading the red copy (header comment line 21 says so). Any future fix (e.g. the documented continuity approximation at red_noise_source.cpp:96-98) has to be made twice.

Recommendation: Extract a small value type, e.g. struct SignedDraw { float lastSign{-1.f}, lastVal{0.f}; float next(Randomizer&, float boost, float continuity, float zeroCross); }; hold one in each class and call it from both sites. Byte-identical output is preserved because the draw order is unchanged.


## F244 [UNVERIFIED] medium (reporter: medium) duplication — engine/include/mforce/source/white_noise_source.h:98

**Noise family copies: white/red shaping block, Wander1/Wander3 pin plumbing**

Reporters: arch-duplication

Evidence: `white_noise_source.h:98-117 vs red_noise_source.cpp:77-100: `lastSign_ = float(rng_.sign()); if (lastSign_ == 0.0f) lastSign_ = 1.0f;` ... `rng_.range(lastValue_, nextValue_, lastValue_, influence)`; wander_noise_source.h WanderNoiseSource 21-57 vs WanderNoise3Source 256-292 identical descriptor/set_param/get_param plumbing (~35 lines).`

Why: The sign/boost/continuity shaping is the behaviour Matt pinned as his own (RedNoise); having it in two nodes means a retune of one silently leaves the other on the old behaviour.

Recommendation: A shared `NoiseShaper` (sign hold, boost, continuity) used by White and Red; a `WanderNoiseBase` holding the common pins for Wander1/2/3.


## F245 [UNVERIFIED] low (reporter: low) modern-cpp — engine/include/mforce/util/fft.h:11

**util nits: C-cast and hand-typed pi in fft.h; dead counter and always-true branch in signal_stats.h; raw pointer+length instead of std::span**

Reporters: render-instrument

Evidence: `fft.h: `inline void fft_inplace(std::vector<std::complex<float>>& x)` (11), `int n = (int)x.size();` (12), `float ang = -2.0f * 3.14159265358979f / float(len);` (22), float twiddle recurrence `w *= wlen;` (31). signal_stats.h: `int nonzero = 0;` incremented unconditionally after the floor clamp (70, 78) so it always equals Nover2-1; `if (magSum > 0.0)` (80) is unconditionally true because every mag is floored to 1e-12f (74); `compute_time_stats(const float* samples, int n, ...)` (21).`

Why: Cheap readability/correctness-of-intent debt in C++20 code: `std::numbers::pi_v<float>` and `std::span` are the idioms; the misnamed `nonzero` and dead branch mislead a reader into thinking zero bins are excluded from the flatness geomean. The float twiddle recurrence accumulates error across 8192-point stages — acceptable for stats, worth a comment so no one reuses it for analysis that needs precision.

Recommendation: Take `std::span<std::complex<float>>` / `std::span<const float>`; use `std::numbers::pi_v<float>`; rename `nonzero` to `bins` and drop the `magSum > 0.0` guard (or genuinely exclude floored bins if that was the intent); recompute twiddles per stage from `std::polar` with double accumulation if precision ever matters.


## F246 [CONFIRMED-BY-MATT'S-CLAUDE] critical (reporter: critical) soundness — engine/src/additive_source2.cpp:100

**AdditiveSource2 bakes partial frequencies from a stale currFreq_ at prepare, so notes play at the previous note's pitch and frequency modulation is ignored**

Reporters: additive

Evidence: `partialCount_ = std::min(n, int(12000.0f / std::max(currFreq_, 1.0f)));  ...  endFreq_[i] = currFreq_ * endIdx_[i];   (prepare, lines 100/106)  //  pFreq = endFreq_[i] * (1.0f + freqOffset_[i]);   (compute_wave_value, line 175)`

Why: WaveSource::prepare (dsp_wave_source.h:20-28) never writes currFreq_; only WaveSource::next (line 46) does, and the member defaults to 440 (line 106). So the first note on an AdditiveSource2 renders at 440 Hz × index regardless of the frequency param, every later note renders at the preceding note's pitch, and a frequency envelope or bend has no effect for the note's duration because compute_wave_value reads the prepare-time endFreq_ array, never currFreq_. The one patch using this type, patches/baselines/as2_bright_attack_test.json, sets frequency 165.0 and therefore renders at 440 Hz with 27 partials instead of 60.

Recommendation: Compute per-sample from the live frequency exactly as Partials does (pfreq = currFreq_ * endIdx_[i], and startIdx_ likewise) and delete endFreq_/startFreq_. If prepare-time sizing of partialCount_ is wanted, read frequency_->current() after WaveSource::prepare rather than the currFreq_ member, and re-evaluate the cutoff per sample.


## F247 [UNVERIFIED] medium (reporter: medium) performance — engine/src/additive_source2.cpp:182

**Legacy sources pay CRT transcendental calls per partial per sample (pow, fmod, sin, floor) — the same costs partials.h measured and eliminated**

Reporters: additive

Evidence: `pAmpl = (1.0f / std::pow(float(i + 1), aEnvVal)) * std::pow(aEnvVal, 2.0f);  ...  partialPos_[i] += pFreq / float(sampleRate_) + float(i) * 0.00001f * std::fmod(po, 1.0f);  val += std::sin(partialPos_[i] * TAU) * pAmpl;   (additive_source2.cpp:182-189)  //  float ampl = weight * (ro == 0.0f ? 1.0f : (1.0f / std::pow(float(pnum), ro)));  ... partialPos_[i] -= std::floor(partialPos_[i]); val += std::sin(partialPos_[i] * TAU) * ...   (basic_additive_source.h:146-166)`

Why: AdditiveSource2 defaults to 500 partials in rolloff mode (registry: set_default_partials(500)), so each sample issues up to 2×partialCount pow calls plus a fmod and a sin per partial; BasicAdditiveSource issues pow + floor + sin per partial (up to 12000/f0 partials). partials.h documents that a single CRT truncf per partial cost 1.4-1.5x on the whole loop (676-687) and that fast_sin_turns replaced libm sin (782-784). Compared with AdditiveSource these nodes are an order of magnitude slower for the same spectrum.

Recommendation: If either source survives the redundancy finding: cache rolloff per partial on envelope change (as Partials does), hoist fmod(po, 1) out of the loop, use the int-cast wrap and fast_sin_turns, and store per-partial state in float rather than std::vector<bool> (absAmpl_).


## F248 [UNVERIFIED] high (reporter: high) soundness — engine/src/additive_source2.cpp:187

**AdditiveSource2 phase accumulator is never wrapped, so float precision degrades through every sustained note**

Reporters: additive

Evidence: `partialPos_[i] += pFreq / float(sampleRate_) + float(i) * 0.00001f * std::fmod(po, 1.0f);  ...  val += std::sin(partialPos_[i] * TAU) * pAmpl;`

Why: partialPos_ grows without bound (no floor/trunc as in BasicAdditiveSource line 164 and Partials lines 699-700). A 12 kHz partial passes 1e5 cycles in about 8 s, where a float ulp is ~0.008 cycles (≈3° of phase) and the argument passed to std::sin exceeds 6e5 rad; quantisation noise grows linearly with note length. This corrupts audio on long holds independently of the prepare-pitch bug above.

Recommendation: Wrap to [0,1) after each advance (x -= float(int(x)); if (x < 0) x += 1), and hoist the per-sample fmod(po, 1) out of the partial loop.


## F249 [UNVERIFIED] low (reporter: low) duplication — engine/src/chord.cpp:24

**ChordDef table defines the same quality twice (dim7 / -7, m7b5 / h7) and the by-name lookup is ambiguous**

Reporters: music-model

Evidence: `{"dim7","dim7","Diminished 7th",   {"1", "m3", "-5", "M6"}},   // line 24
...
{"-7",  "dim7","Diminished 7th",{"1", "m3", "-5", "-7"}},       // line 36 — M6 and -7 are both 9 semitones (music.cpp:137,143)`

Why: Lines 25 (`m7b5`, Half Diminished) and 37 (`h7`, Half-Diminished 7th) are also identical interval sets. ChordDef::get matches `cd.name == name` first-wins (49-50), so get("Diminished 7th") returns whichever is first. Line 82 `canonic.chords["M"] = ChordDef::get("")` repeats what 79-81 already did.

Recommendation: One entry per quality with an alias list on ChordDef (or a separate alias map); drop line 82.


## F250 [CONFIRMED-BY-MATT'S-CLAUDE] critical (reporter: critical) soundness — engine/src/chord.cpp:204

**Chord::init_pitches loops forever whenever the chord has fewer distinct pitch classes than tones (power chord and every guitar/piano dictionary voicing)**

Reporters: music-model

Evidence: `while ((int)positions.size() < N) {
    int p = positions.back() + (spread + 1);
    while (voicedPCs.count(pcAtPos(p))) ++p;   // pcAtPos(p) = tones[p % N] (+12k) % 12 — only ever yields the pitch classes already in `tones``

Why: pcAtPos cycles through the pitch classes of `tones`, so once every distinct class is voiced the inner `++p` loop never exits (and eventually overflows int). ChordDef "5" (chord.cpp:19, tones {0,7,12} -> classes {0,7}) and every Guitar-Bar-6/5/4 and Piano entry (chord.cpp:90-96, 104-111, 119-124, 139-143, all listing "8"/"12"/"15" octave duplicates) have fewer classes than N. Reachable today: `mforce_cli --josie` default chord string uses `_g5_`/`_g6_`/`_g4_` tokens (tools/mforce_cli/main.cpp:365-373) -> parse_util.h:153 -> Chord::create(dictName,...) chord.cpp:236-248 -> init_pitches. The pitch-class walk landed in c510c4e after the dictionaries existed and was never validated against them.

Recommendation: Make the walk terminate: dedupe `tones` by pitch class before the walk (or stop requiring a new class once all classes are voiced and allow octave doublings), and add a test that calls init_pitches for every ChordDef in every dictionary with inversion 0..N-1 and spread 0..2.


## F251 [UNVERIFIED] low (reporter: low) smell — engine/src/full_additive_source.cpp:10

**formantWeight_ is left null in the constructor while formantFloor_ is defaulted, forcing null checks and a nullptr get_param result**

Reporters: additive

Evidence: `: WaveSource(sampleRate), formantFloor_(std::make_shared<ConstantSource>(1.0f)), noiseRng_(seed) {}`

Why: Every other ValueSource param in this unit is default-constructed to its descriptor default; here prepare (line 18) and compute_wave_value (line 52) must guard `if (formantWeight_)`, and get_param("formantWeight") (full_additive_source.h:65) returns nullptr for an unwired node, which generic serialisation/UI code elsewhere treats as 'no value' rather than the descriptor default 0.0.

Recommendation: Initialise formantWeight_ to ConstantSource(0.0f) in the constructor and drop the two null checks.


## F252 [UNVERIFIED] high (reporter: high) rt-safety — engine/src/hybrid_ks_source.cpp:35

**HybridKSSource allocates and runs an unbounded DFT inside the render path**

Reporters: sources-physical

Evidence: `fill_table(): `table_.resize(len);` (called from compute_ks when ptr_==0, i.e. from next()); extract_partials(): `partialAmpl_.resize(numPartials_); partialPhase_.resize(numPartials_); startAmpl_.resize(numPartials_);` then `for (k < numPartials_) for (n < N) { re += table_[n]*std::cos(angle); im -= ... }` — all reached from compute_wave_value() -> compute_ks() at lines 89 and 111.`

Why: Direct violation of the no-heap/no-unbounded-work rule for per-sample paths. Three vector resizes happen on the first sample of every note and at the KS->additive transition; the transition sample also pays numPartials x N sin/cos pairs (200 partials x 960 samples at 50 Hz = 192k trig calls in ONE tick) plus fill_table pulls `len` samples from inputSource_ in one tick. In the live voice pool this is a guaranteed audio-callback spike at every note start and every transition. prepare() also calls `table_.clear()` (line 18) and `targetAmpl_.resize` (line 29) inside the g_audioMutex critical section the callback waits on.

Recommendation: Reserve table_ for sr/minFreq and the three partial vectors for the descriptor max (200) in the constructor; never resize after. Replace the one-shot DFT with per-sample Goertzel accumulators updated during the last hold cycle so the transition costs O(numPartials) per sample instead of O(numPartials x N) once. If the node stays experimental, at minimum drop it from the live UI menu (mforce_ui/main.cpp:10796) until it is allocation-free.


## F253 [UNVERIFIED] low (reporter: low) smell — engine/src/hybrid_ks_source.cpp:107

**HybridKSSource carries dead debug code, an unused RNG, duplicate setters, and a loader special case**

Reporters: sources-physical

Evidence: `lines 107-109 `float tblPeak = 0; for (float v : table_) if (...) tblPeak = ...;` and 115-117 `float maxA = 0; ... maxA = partialAmpl_[k];` — neither value is read. hybrid_ks_source.h:97 `Randomizer rng_;` is initialised at .cpp:12 and never used. Header :28-32 public setters duplicate set_setting (:71-75), and patch_loader.cpp:981-990 special-cases the type (`hks->set_hold_cycles(p.value("holdCycles", 5)); ...`) even though the registry (source_registrations.cpp:571) plus the generic setting path handle the same keys. .cpp:38 `table_[i] = inputSource_->next();` dereferences without the null check prepare() uses (:16).`

Why: Dead per-cycle loops run in the render path (the tblPeak scan is O(N) at every transition), the loader special case is a second source of truth for the node's defaults, and the setters/compute_additive index `startAmpl_[k]` and `targetAmpl_[k]` (:134) by a live numPartials_ while the vectors were sized at the previous prepare/extract — a UI set_setting raising numPartials mid-note reads out of bounds.

Recommendation: Delete the two debug loops and rng_; drop the public setters and the loader branch in favour of the generic settings path (targetPartials becomes an ArrayDescriptor); bound the compute_additive loop by `std::min(numPartials_, int(startAmpl_.size()))` or re-size in set_setting under the lock.


## F254 [UNVERIFIED] high (reporter: high) rt-safety — engine/src/mixer.cpp:18

**Mixer::render allocates a scratch vector on every block**

Reporters: arch-modern-cpp, filters

Evidence: `std::vector<float> mono(frames);`

Why: Per-block heap allocation in a render path; today Mixer::render is only reached from the offline CLI (the UI mixes node graphs itself at main.cpp:3966), so the practical cost is allocator churn rather than dropouts, but it blocks ever reusing the engine mixer live.

Recommendation: Keep a member scratch buffer sized in a prepare()/set_block_size step, or render channels directly into the interleaved output.

Also reported as: StereoMixer::render allocates a scratch vector on every call


## F255 [UNVERIFIED] high (reporter: high) soundness — engine/src/mixer.cpp:28

**StereoMixer advances master gainL/gainR once per channel per frame, so with 2+ channels a modulated master gain runs N× too fast and channels see different gain samples**

Reporters: filters

Evidence: `for (auto& ch : channels) {
  ...
  for (int i = 0; i < frames; ++i) {
    float gl = gainL->next();
    float gr = gainR->next();`

Why: gainL/gainR are prepared once (lines 15-16) but next() is called channels.size() * frames times. They are arbitrary ValueSources (patch_loader.cpp:1701-1702 and 1765-1766 resolve them via resolve_param_or, which accepts any node ref), so an envelope or LFO on the master gain would be consumed at N× rate and channel k would be scaled by a different sample than channel 1 — a silent audio-corruption bug in the engine's only mixer. The UI's hand copy (tools/mforce_ui/main.cpp:3958-3959) gets the ordering right, evaluating gl/gr once per frame outside the channel loop. Latent today because every current patch leaves the master gains constant, but the structure is wrong.

Recommendation: Invert the loops (frames outer, channels inner) or pre-evaluate gainL/gainR into a per-frame scratch before the channel loop; render each channel's mono block first, then do one per-frame pass that reads gl/gr once and accumulates all channels.


## F256 [CONFIRMED-BY-MATT'S-CLAUDE] high (reporter: high) duplication — engine/src/mixer.cpp:36

**StereoMixer::render is hand-duplicated per-sample in mforce_ui and the two copies have already diverged (engine unity-at-center pan vs UI's old -3 dB-center law)**

Reporters: filters

Evidence: `mixer.cpp:40-42: constexpr float kRoot2 = 1.41421356237309504880f; float aL = kRoot2 * std::cos(t * 0.5f * pi); float aR = kRoot2 * std::sin(...)
tools/mforce_ui/main.cpp:3953-3967: "per-sample mirror of the engine's StereoMixer::render (mixer.cpp)" ... float aL = std::cos(t * 0.5f * 3.14159265358979323846f); float aR = std::sin(t * 0.5f * ...);`

Why: The engine law was changed 2026-09-14 (comment at mixer.cpp:36-39) so that center-panned channels write x1.0 and 'WAV loudness == UI's unity-mono monitoring'. The UI mirror was not updated, so a center-panned Channel→Mixer node graph monitors at 0.707 (-3 dB) in the UI while the CLI WAV renders it at 1.0 — exactly the mismatch that change was meant to remove. The copy exists because StereoMixer takes unique_ptr<MonoSource> block renderers and allocates per call (line 18), so the per-sample audio callback cannot reuse it. Second location named: tools/mforce_ui/main.cpp:3957-3971.

Recommendation: Factor the per-sample law into a tiny inline helper in render/mixer.h (e.g. pan_gains(p, aL, aR) returning the unity-center equal-power pair, plus the gl/gr/soft_clip combine) and call it from both StereoMixer::render and the UI's fill_audio_buffer; or give StereoMixer a per-sample pull API the UI can drive. Either removes the drift and the gain-advance bug in one change.


## F257 [UNVERIFIED] medium (reporter: medium) soundness — engine/src/music.cpp:35

**Pitch::from_note_number throws for negative input and truncates fractional input; this is the documented crash path for two strategies**

Reporters: arch-music-model

Evidence: `music.cpp:35-38 `Pitch Pitch::from_note_number(float nn) { int n = int(nn); return {&PitchDef::get(n % 12), n / 12}; }` -- composer.h:1796-1799: 'nothing stopped it descending past note number 0, where Pitch::from_note_number does `n % 12` on a negative and hands PitchDef::get an offset that does not exist' (the 'Unknown PitchDef offset: -3' crash in the phrase_aware and AFS strategies).`

Why: A data-model primitive that throws on a legal float domain turns a musical range problem into a process abort; `int(nn)` also maps 59.9 to B3 instead of C4, so any fractional note number (bends, accidentals applied as floats) rounds the wrong way.

Recommendation: Use floor division with a positive modulo (`int n = int(std::lround(nn)); int pc = ((n % 12) + 12) % 12; int oct = (n - pc) / 12;`) and decide a clamp policy for out-of-range note numbers at the call sites that walk registers.


## F258 [UNVERIFIED] low (reporter: low) modern-cpp — engine/src/music.cpp:160

**Out-of-line static constants and manual init flags where constexpr inline variables and function-local statics would remove SIOF risk and the non-thread-safe lazy init**

Reporters: music-model

Evidence: `const Meter Meter::M_4_4{4, 4};   // music.cpp:160; used as a default member initializer at structure.h:199 `Meter meter{Meter::M_4_4};`
...
static bool s_keysInit = false;   // music.cpp:91; chord.cpp:57 `static bool s_dictsInit = false;``

Why: Meter is a literal type, so M_4_4 etc. can be `static constexpr` in the header (C++17 inline variables), removing a cross-TU dynamic-init dependency that bites the moment a Section/Piece gets static storage. s_keysInit/s_dictsInit (music.cpp:91-95, chord.cpp:57,66-68) are a hand-rolled, data-racy version of C++11 magic statics; the namespace-scope vectors/maps (music.cpp:8,50,131; chord.cpp:12,56) have the same SIOF exposure. ScaleDef also stores integer semitone steps as float (basics.h:134-135, all data at music.cpp:51-71 are ints), which is why tolerance hacks exist at basics.h:180 (`> 1.5f // for float safety`), 213, pitch_walker.h:53/65 and the truncation at chord.cpp:265.

Recommendation: `static constexpr Meter M_4_4{4,4};` in the header; wrap each table in a function returning a function-local static; make ScaleDef steps `int` and drop the tolerances.


## F259 [UNVERIFIED] low (reporter: low) duplication — engine/src/patch_loader.cpp:58

**WaveSourceMono and ValueSourceMono are identical; add_mono's dynamic_pointer_cast chooses between two copies of the same class**

Reporters: render-loader

Evidence: `58-65 and 67-74 have the same body (`src->prepare(ctx, frames); for (...) out[i] = src->next();`); dsp_wave_source.h:9 `struct WaveSource : ValueSource` so a shared_ptr<WaveSource> converts to shared_ptr<ValueSource>. 561-563 `auto ws = std::dynamic_pointer_cast<WaveSource>(src); if (ws) ... WaveSourceMono ... else ... ValueSourceMono`.`

Why: Dead distinction that costs an RTTI cast per node at load and invites the reader to look for a behavioral difference that does not exist.

Recommendation: Delete WaveSourceMono and the cast; add_mono becomes one make_unique<ValueSourceMono>.


## F260 [UNVERIFIED] high (reporter: high) soundness — engine/src/patch_loader.cpp:70

**MonoSource/StereoMixer re-prepare the entire graph on every render() call; no block-streaming contract exists below the voice, which blocks a JUCE/host integration for mixer-mode patches**

Reporters: arch-valuesource-graph

Evidence: `void render(const RenderContext& ctx, float* out, int frames) override { src->prepare(ctx, frames); for (int i = 0; i < frames; ++i) out[i] = src->next(); }

mixer.cpp:15-22: gainL->prepare(ctx, frames); gainR->prepare(ctx, frames); std::vector<float> mono(frames); for (auto& ch : channels) { ch.volume->prepare(ctx, frames); ch.pan->prepare(ctx, frames); ...

render_context.h:11-13: struct RenderContext { int sampleRate; };

multi_source.h:49: e.source->prepare(ctx, frames + e.delaySamples);`

Why: `prepare(ctx, frames)` conflates two things: reset-state-for-a-new-note and here-is-the-note-length (Envelope lays stages out over `frames`, CombinedSource::Fade and CrossfadeSource divide by it, MultiSource rewrites it per entry). A host that calls render() per 512-frame block would restart every envelope and zero every delay line each block. The engine currently escapes this only because the UI streams a bare mono node and the Instrument path drives next() itself; the mixer/channel/MonoSource layer — the only place the engine has a block API — is offline-only by construction. For a JUCE AudioProcessor you need prepareToPlay(sr, maxBlock) → processBlock(n) with note-on resets inside, i.e. the reset and the note-length must be separate calls.

Recommendation: Split the lifecycle: `prepare(const RenderContext&)` (sample rate, max block, allocate once) vs `note_on(int durSamples)` / `reset()` (per-note state) and optionally `process(float* out, int n)` with a default per-sample loop. Give RenderContext the block size and a tick counter (which the sharing fix above also needs). Make StereoMixer::render and the MonoSource adapters stream: prepare once, render many. The PitchedInstrument streaming path is already the model — hoist its prepare-once/next-many contract into the base interface so mixer-mode patches get it too.


## F261 [UNVERIFIED] low (reporter: low) workaround-hack — engine/src/patch_loader.cpp:89

**Tap binding is threaded through a thread_local global with an RAII scope rather than a build context; several process-global mutable statics in the loader**

Reporters: arch-valuesource-graph

Evidence: `static thread_local std::vector<TapBind>* t_tapBinds = nullptr;
struct TapBindScope { ... t_tapBinds = cur; } ~TapBindScope() { t_tapBinds = prev; } };
// The collector is build-scoped via RAII rather than threaded through the ~40 resolve_param call sites

patch_loader.cpp:617-618: static bool registered = false; if (!registered) { register_all_sources(); registered = true; }
patch_loader.cpp:545: static std::unordered_set<std::string> warned;
patch_loader.cpp:802: static std::unordered_set<std::string> warnedGates;`

Why: The thread_local is a confessed shortcut (comment at 84-87) for the real missing abstraction — a `BuildContext {valueNodes, usage, tapBinds, perf, onsets}` that resolve_param, configurators and build_graph all need and currently receive as 3-5 loose parameters each. The process-global `warned` sets mean the UI, which reloads patches hundreds of times per session, warns about an unknown key only on the first patch that has it; the lazy `registered` flag duplicates the 13 explicit register_all_sources() calls in the UI and is not thread-safe (the UI's audio and UI threads both can load).

Recommendation: Create a `LoadContext` struct passed by reference through resolve_param/wire_params_generic/configurators (ResolveParamFn already closes over most of it) and put tapBinds, usage and warn-sets on it; move registration to an explicit `SourceRegistry::ensure_registered()` guarded by std::call_once.


## F262 [UNVERIFIED] medium (reporter: medium) duplication — engine/src/patch_loader.cpp:98

**The shared-source auto-RefSource rule is implemented twice (engine loader and UI) and the JSON ref-edge scanner four times inside the loader**

Reporters: arch-valuesource-graph

Evidence: `patch_loader.cpp:104: // Mirrors the auto-wrap logic in mforce_ui/main.cpp::update_all_dsp().
tools/mforce_ui/main.cpp:1062-1077: // For each output with >1 consumer, first consumer keeps real source, rest get RefSource wrappers ... auto ref = std::make_shared<RefSource>(srcNode->dspSource);

JSON ref scanners: patch_loader.cpp:190-215 (collect_advance_ids `scan`/`scanIds`), 232-256 (promote_starved_refs `node_refs` scanRefs/scanIds), 1104-1113 (extract_subgraph_json `scanRefs`), 1375-1388 (bind_wiring `reaches_freq` inline loop) — each re-implements 'find every {"ref":id} / inputs id under a node'.`

Why: The advancer rule is a semantic contract (who ticks a shared node) that must be identical in the two graph builders or the UI audition and the CLI render disagree — the audition-path-mismatch family the project has already paid for. The two copies are already not identical: the UI skips NT_PERFORM sources (main.cpp:1072) and taps (1054); the loader counts PerformNode refs by node id. The four JSON scanners differ subtly too (collect_advance_ids and node_refs accept `inputs` string ids; extract_subgraph_json does not, so a Multiplex template whose nodes use the `inputs` convention is extracted incomplete).

Recommendation: Fold the sharing rule into the engine (see the per-tick memo finding — then neither copy is needed). Until then, expose one engine function the UI calls (e.g. `wrap_secondary_consumers(root, …)`) instead of mirroring it. Replace the four scanners with one `for_each_ref(const json& node, F&&)` that handles both {"ref"} and `inputs` forms, used by collect_advance_ids, promote_starved_refs, extract_subgraph_json and bind_wiring.


## F263 [UNVERIFIED] high (reporter: high) soundness — engine/src/patch_loader.cpp:126

**Tap edges create shared_ptr ownership cycles; every voice graph containing a feedback loop leaks on rebuild**

Reporters: arch-valuesource-graph

Evidence: `auto rs = std::make_shared<RefSource>(nullptr, true); t_tapBinds->push_back({rs, ...});
patch_loader.cpp:1073: tb.ref->source = it->second;   // shared_ptr<ValueSource>
dsp_value_source.h:156: std::shared_ptr<ValueSource> source;
delay_line_source.h:177: std::shared_ptr<ValueSource> members_[kMaxMembers];`

Why: A tap-closed loop is DelayLine → (shared_ptr) Shaper → (shared_ptr) Sum → (shared_ptr) RefSource{guard} → (shared_ptr) DelayLine: a strong cycle that no destructor ever breaks. DelayLineSource::members_ (when compensate is on) adds a second strong path around the same loop. The UI rebuilds the whole instrument (polyphony voices) on every graph edit (main.cpp:4642-4655 'Instrument rebuilt') and voice_gc (main.cpp:4151-4165) only drops its own handles, so each rebuild of a loop patch orphans polyphony × loop-subgraph nodes with their 4096-float KS buffers / sr/20 delay buffers. The CLI leaks once per load, which is harmless, but the UI session leaks continuously while editing the flute/oboe/trombone family that is now the project's best material.

Recommendation: Make the back-edge weak: `std::weak_ptr<ValueSource> source` in the guarded (tap) RefSource, locked once in prepare() into a raw pointer for the sample loop (the voice graph owns the target for the voice's lifetime, so a raw cached pointer is safe between prepares), and `ValueSource* members_[]` in DelayLineSource. Alternatively give VoiceGraph an explicit `teardown()` that nulls every RefSource::source before the pool drops it. Add a test that builds a tap loop, drops all roots, and asserts a weak_ptr to the DelayLine expires.


## F264 [UNVERIFIED] medium (reporter: medium) duplication — engine/src/patch_loader.cpp:190

**Four hand-rolled recursive JSON edge scanners with diverging semantics (ref / inputs / tap coverage differs per copy)**

Reporters: render-loader

Evidence: `190-214 collect_advance_ids (`scan` handles ref+tap, `scanIds` handles inputs); 235-255 promote_starved_refs::node_refs (`scanRefs` handles ref only, `scanIds` inputs); 1104-1113 extract_subgraph_json::scanRefs (ref only — ignores plain-string 'inputs' ids and 'tap'); 1358-1390 bind_wiring::reaches_freq (one level of object/array under params, no nested recursion). Each is a `std::function<void(const json&)>` recursive lambda.`

Why: When the tap edge form was added it reached two of the four scanners; extract_subgraph_json would drop a template node referenced only through 'inputs'. Four places to keep in step for every new edge form, and the mismatch is invisible until a patch happens to use the uncovered form inside a Multiplex template or a reaches_freq chain.

Recommendation: One `template<class F> void for_each_edge(const json& node, F&& fn)` yielding `(EdgeKind{Ref, Input, Tap}, const std::string& id)` over params+inputs; implement collect_advance_ids, advancer/reachable, extract_subgraph_json and reaches_freq on it.


## F265 [UNVERIFIED] medium (reporter: medium) duplication — engine/src/patch_loader.cpp:190

**Recursive JSON ref/tap walker copied three times and the voice-pool build copied twice ('see render-path twin above')**

Reporters: arch-modern-cpp

Evidence: `190: `std::function<void(const json&)> scan = [&](const json& v) {` with the same body at 235 (`scanRefs`) and 1104 (`scanRefs`), plus scanIds at 206 and 247; voice-pool construction 1534-1540 `for (int v = 0; v < polyphony; ++v) { PitchedInstrument::VoiceGraph vg; PerformContext perf = make_perform_context(...); auto g = build_graph(...)` repeated at 1855-1860, with comments at 1868/1880/1882 "see render-path twin above"`

Why: The tap/ref scanning rules (which decide advancer ownership and starved-ref promotion) are soundness-critical; three copies mean a fix to one silently misses the others. The two voice-pool builders already differ (render vs live) only in surroundings, and the comments admit the duplication.

Recommendation: Factor `for_each_ref(const json&, callback)` (a plain recursive function, no std::function) and a `build_voice_pool(nodeMap, nodeOrder, sampleRate, inst, polyphony)` helper used by both paths.


## F266 [UNVERIFIED] medium (reporter: medium) build — engine/src/patch_loader.cpp:398

**Transitive-include reliance and unused includes**

Reporters: arch-modern-cpp, render-loader

Evidence: `398: `std::strlen(desc.enum_labels[li])` plus std::tolower/std::equal/std::sin/std::lround used with includes 33-40 limited to `<nlohmann/json.hpp> <fstream> <functional> <cstdio> <sstream> <stdexcept> <unordered_map> <unordered_set>` (no <cstring>, <cctype>, <algorithm>, <cmath>); conductor.h:393 `snprintf(buf, sizeof(buf), "%g", chord.dur);` and parse_util.h:158 without <cstdio>; unused: red_noise_source.cpp:4 `#include <iostream>`, dun_parser.h:11 `#include <iostream>`, phased_value_source.h:9 `#include <stdexcept>``

Why: Compiles today only because nlohmann/json.hpp happens to pull those headers; an MSVC STL or nlohmann update breaks the build in files that look unrelated. <iostream> in a hot DSP .cpp adds static-init cost for nothing.

Recommendation: Include what you use (add the four headers to patch_loader.cpp, <cstdio> to conductor.h/parse_util.h); drop the unused includes; consider clang-tidy misc-include-cleaner in CI.

Also reported as: Standard-library symbols used without their headers; compiles on MSVC only via transitive includes


## F267 [UNVERIFIED] medium (reporter: medium) smell — engine/src/patch_loader.cpp:432

**wire_params_generic contains type-specific blocks dispatched by dynamic_cast (Shaper invariants, Partials expandRule) although the registry provides a per-type configurator hook for exactly this**

Reporters: render-loader

Evidence: `432 `if (dynamic_cast<ShaperSource*>(&src)) {` ... 470 (values2/segs2 length and type checks, hysteresis warnings); 475-500 `if (params.contains("expandRule")) { if (auto* host = dynamic_cast<Partials*>(&src)) {` with an inline 12-field ExpandRule parse. source_registry.h:23-26 JsonConfigurator exists and is used for CombinedSource/MultiSource/PhasedValueSource etc.`

Why: The 'generic' function now has to include shaper_source.h and partials.h and grows a branch per type that needs validation; the configurator table is the one place a reader looks for per-type JSON handling and finds neither of these.

Recommendation: Register a 'Shaper' configurator carrying 432-470 and a shared `partials_expand_rule_configurator` attached to FullPartials/SequencePartials/ExplicitPartials/CompositePartials carrying 475-500; wire_params_generic then needs no concrete-type includes.


## F268 [UNVERIFIED] medium (reporter: medium) soundness — engine/src/patch_loader.cpp:509

**Unknown-key warning lives inside wire_params_generic, so the special-case branches that never call it (VarSource, RangeSource, FormantSpectrum, FixedSpectrum, PerformNode) silently ignore typos; allowlist is hand-grown**

Reporters: arch-hacks-census

Evidence: `patch_loader.cpp:545-553 `static std::unordered_set<std::string> warned; for (auto it = params.begin(); ...) if (!known(it.key()) ...` is reached only through wire_params_generic; VarSource :656-665 and RangeSource :666-675 build from `resolve_param_or(p, "val", ...)` with no wire_params_generic call; :510-530 `static const std::unordered_set<std::string> kStructural = { ... // grown empirically until the whole gate corpus loads warning-free``

Why: Backlog 67 shipped the warning as the fix for 'engine older than patch silently plays a downgraded patch', but it only covers types that route through the generic pass. A misspelled 'varPct' on a VarSource, or any key on a PerformNode, is still silently dropped — the exact failure the warning was built to end. The empirically grown kStructural list is a second hand-maintained registry of 'keys consumed somewhere' that backlog 31 already calls a lint gap.

Recommendation: Hoist the unknown-key check out of wire_params_generic into build_graph after each branch (one call per node, independent of path), and replace kStructural with per-type 'branch-consumed keys' declared next to the branch (or returned by the configurator) so the allowlist cannot drift from the code that consumes the keys.


## F269 [CONFIRMED-BY-MATT'S-CLAUDE] high (reporter: high) god-class — engine/src/patch_loader.cpp:605

**build_graph is a 460-line if/else type switch that pre-empts the registry; 15 registered types never reach their registry factory or configurator**

Reporters: arch-valuesource-graph

Evidence: `patch_loader.cpp:656 if (type == "VarSource") ... 666 else if (type == "RangeSource") ... 838 else if (type == "BWLowpassFilter" || ...) ... 1050 else if (reg.has(type)) { auto src = reg.create(type, sampleRate, seed); ... configurator ... }

source_registrations.cpp:417-424: reg.register_type("BWLowpassFilter", ..., [](ValueSource& src, const nlohmann::json& p, const ResolveParamFn& resolve) { if (p.contains("cutoff")) src.set_param("cutoffFreq", resolve(p.at("cutoff"))); });

patch_loader.cpp:849-851: // JSON uses "cutoff" alias for single-band filters
 if (pp->contains("cutoff")) src->set_param("cutoffFreq", resolve_param(pp->at("cutoff"), valueNodes, &usage));`

Why: VarSource, RangeSource, Envelope, CurveNode, SegmentSource, Vibrato, BWLowpass/Highpass/Bandpass, FormantSpectrum, FixedSpectrum, FormantSequence, AdditiveSource2, WavetableSource, HybridKSSource and MultiplexSource are all registered (source_registrations.cpp) AND special-cased before `reg.has(type)`, so their registry factories and JsonConfigurators are unreachable from the loader (get_configurator has exactly one caller, patch_loader.cpp:1054). The BW 'cutoff' alias is implemented twice, once dead. The VarSource/RangeSource branches (656-675) are pure redundancy — the registry factory plus wire_params_generic already handle val/var/varPct and the absolute/normalized settings — and because they skip wire_params_generic they also skip the unknown-key warning. The registry therefore does not scale as a registry: adding a node with any structural config means touching source_registrations.cpp AND this chain, and the two have no mechanism to stay in sync.

Recommendation: Move every per-type JSON branch into that type's JsonConfigurator (the hook already receives params + resolve) so build_graph becomes: create via registry → wire_params_generic → configurator. Where a type needs constructor args from JSON (BW `sections`, Vibrato), give the factory the params object or make them settings with a rebuild. Delete the VarSource/RangeSource branches outright (verify with the library null gate). Keep only PerformNode (needs PerformContext) and MultiplexSource (needs nodeMap) as loader-level special cases, and consider passing a `LoadContext` to configurators so even those move.


## F270 [UNVERIFIED] medium (reporter: medium) smell — engine/src/patch_loader.cpp:617

**Registry population is ad hoc: a check-then-set static bool in build_graph plus 14 explicit register_all_sources() calls in the tools; register_type silently overwrites duplicates**

Reporters: render-loader

Evidence: `617-618 `static bool registered = false; if (!registered) { register_all_sources(); registered = true; }`; source_registry.h:61-62 'Call once at startup'; tools/mforce_ui/main.cpp calls register_all_sources() at 13 sites (12981…13518) and mforce_cli/main.cpp:1027 once; source_registry.cpp:16 `entries_[type_name] = Entry{...}` overwrites without complaint.`

Why: Three initialization conventions for one singleton; each UI call site rebuilds ~75 std::function entries. The plain bool is not a thread-safe once (no concurrent loading exists today, so this is latent). Silent overwrite means a mistyped alias (e.g. registering 'KSPianoString' twice) is never noticed.

Recommendation: Make `SourceRegistry::instance()` self-populating (`static SourceRegistry reg = []{ SourceRegistry r; populate(r); return r; }();` — a C++11 magic static, thread-safe), remove register_all_sources() from the public header and all 15 call sites, and make register_type throw on a duplicate name.


## F271 [UNVERIFIED] low (reporter: low) modern-cpp — engine/src/patch_loader.cpp:617

**Hand-rolled `static bool` lazy-init guards instead of C++11 magic statics / std::call_once (four sites)**

Reporters: arch-hacks-census

Evidence: `patch_loader.cpp:617-618 `static bool registered = false;
    if (!registered) { register_all_sources(); registered = true; }`; chord.cpp:57 `static bool s_dictsInit = false;` (:66-68); music.cpp:91 `static bool s_keysInit = false;` (:93-95); smooth_voicing_selector.h:114 `static bool warned = false;``

Why: The check-then-set is a data race if two threads ever build a graph or init dictionaries concurrently (the UI's instrument cache rebuild and a future background render would do exactly that); C++ guarantees thread-safe initialization only for the static's own initializer. Today all callers are on the UI/CLI main thread, so this is latent, not live, but it is the cheapest class of bug to remove.

Recommendation: Replace with `static const bool once = (register_all_sources(), true);` or `std::call_once`, and make ChordDictionary/Key tables function-local statics initialized by a builder function.


## F272 [CONFIRMED-BY-MATT'S-CLAUDE] high (reporter: high) workaround-hack — engine/src/patch_loader.cpp:656

**build_graph's special-case chain re-implements wiring that the types' own descriptors already provide, bypasses reg.create so default seeds and constructor defaults are triplicated, and leaves the registry's BW-filter configurators dead**

Reporters: arch-hacks-census, render-loader

Evidence: `VarSource 656-665 / RangeSource 666-675 hand-resolve val/var/varPct and min/max/var and never call wire_params_generic, although var_source.h:50-63 and range_source.h:53-60 declare exactly those ParamDescriptors with set_param, and 'absolute'/'normalized' are SettingDescriptors (var_source.h:72-81, range_source.h:75-87); registry factories 178-192 already construct them. Vibrato 829-834 passes 7 p.value() ctor args then 835 `wire_params_generic(*vib, ...)` re-applies the same 7 SettingDescriptors (vibrato.h:57-68); defaults live in vibrato.h, loader, and registrations.cpp:459-461. HybridKS 986-988 `hks->set_hold_cycles(p.value("holdCycles", 5))` etc. after 985 already applied them via hybrid_ks_source.h:62-74. 939 `wt->set_interpolate(p.value("interpolate", false))` after 938 applied the 'interpolate' setting (wavetable_source.h:67-76). 898 `as2->set_default_partials(p.value("partialCount", 500))` after 895 applied 'partialCount' (additive_source2.h:99-108), and registrations.cpp:471 calls set_default_partials(500) a third time. Seeds duplicated loader/registry: SegmentSource 0x5E6A'0000u (821/376), Vibrato 0xF1B0'0000u (834/461), AdditiveSource2 0xADD3'0000u (892/470), WavetableSource 0xC0FFEEu (935/524), HybridKS 0xBEEF'C0DEu (982/573). 'cutoff' alias at 850-851 duplicates registrations.cpp:420-431, and those BWLowpass/BWHighpass configurators can never run because 838 intercepts the type before `reg.has(type)` at 1050.`

Why: The generic path is the design (CLAUDE.md: explicit registries, self-describing nodes) but the loader does not trust it: the 470-line build_graph is a god-function whose branches silently skip the unknown-key warning (VarSource/RangeSource never reach 502-554) and ignore any descriptor added later. Three sources of truth for defaults and seeds guarantee drift; the dead configurators mislead the next reader into editing the wrong place.

Recommendation: (1) Widen JsonConfigurator to take a `LoadContext&` (sampleRate, valueNodes, usage, formantNodes, perf, onsets, nodeMap) so every branch except PerformNode/NameGate/Multiplex (which need loader-level state) moves next to its factory in source_registrations.cpp. (2) Delete the lines that duplicate descriptor-driven settings (656-675 entirely; 829-834 ctor args to defaults; 939; 986-988; 898). (3) Add ArrayDescriptors for SegmentSource.values, HybridKS.targetPartials, FixedSpectrum/BandSpectrum.gains so the generic array pass handles them. (4) Every remaining special case calls `reg.create(type, sampleRate, seed)` first so seeds live only in the registry. (5) Replace the global kStructural allowlist (510-530) with per-type consumed-key lists declared by each configurator, so a typo'd 'attack' on a non-envelope node is warned.

Also reported as: Loader type-name chain (17 special cases) re-implements settings the descriptors already wire, and shadows registry configurators so they are dead code


## F273 [UNVERIFIED] medium (reporter: medium) duplication — engine/src/patch_loader.cpp:680

**Envelope stage-form parser and the RefSource first-consumer-advances rule are each duplicated in mforce_ui/main.cpp; the loader's own comment says only the preset form was shared**

Reporters: render-loader

Evidence: `680-699 (startVal/endVal/power/type ladder/percent/minSec/maxSec/nominal → add_stage) is field-for-field identical to tools/mforce_ui/main.cpp:1368-1387 `envelope_stages_from_json`. Loader 713-715: 'Shared preset dispatch (envelope_json.h) — the UI graph loader calls the same function, so the two cannot drift' — the stage form got no such treatment. Loader 98-124 usage-counter→RefSource vs UI main.cpp:1062-1080 consumers→`std::make_shared<RefSource>(srcNode->dspSource)`; loader 104 admits 'Mirrors the auto-wrap logic in mforce_ui/main.cpp::update_all_dsp()'.`

Why: Two parsers of the same JSON shape means the UI and CLI can accept different stage keys (the UI copy lacks seed/stage_accuracy/ramp_accuracy/timeMode handling at 702-711, so a UI round-trip of a stage-form envelope silently loses those). The RefSource rule is correctness-critical (double-advance) and the two implementations have different exemptions (UI skips NT_PERFORM at 1072; loader has no such rule).

Recommendation: Move stage parsing into envelope_json.h as `envelope_from_stages_json(const json&, int sampleRate)` beside envelope_from_preset_json and call it from both. For the advancer rule, expose a small `AdvancerTracker` helper in core that both the loader's resolve_param and the UI's update_all_dsp use.


## F274 [UNVERIFIED] medium (reporter: medium) duplication — engine/src/patch_loader.cpp:838

**Loader special-case branches shadow registry configurators and re-apply what wire_params_generic already did**

Reporters: arch-duplication

Evidence: `838-852: `else if (type == "BWLowpassFilter" || ...) { ... wire_params_generic(*src, *pp, valueNodes, &usage); if (pp->contains("cutoff")) src->set_param("cutoffFreq", ...)`; the only call of `reg.get_configurator(type)` is at 1054 inside the later `else if (reg.has(type))` branch (1050); source_registrations.cpp:420-431 registers the same "cutoff" alias as configurators for BWLowpass/BWHighpass. Also 939 (set_interpolate), 986-988 (HybridKS settings), 829-835 (Vibrato), 814-822 (SegmentSource), 898 (partialCount) re-set values wire_params_generic/set_setting already applied.`

Why: The BW configurators in the registry are unreachable dead code (no other caller of get_configurator in engine or tools — grep), and the loader's explicit branches duplicate registry knowledge. Two sources of truth for the same alias; the explicit branch list grows with every node type instead of the registry.

Recommendation: Delete the explicit BW/Vibrato/Segment/HybridKS branches and let the registry path handle them (keep only the configurators); audit the remaining explicit branches for the same shadowing.


## F275 [UNVERIFIED] medium (reporter: medium) rt-safety — engine/src/patch_loader.cpp:1041

**Multiplex instances are built lazily on first prepare(): JSON parse plus count× build_graph runs inside the audio-mutex critical section on each voice's first note in the live UI**

Reporters: render-loader

Evidence: `1041 `mux->set_template(subtreeStr, baseSeed, std::move(builder));` — multiplex_source.h:83-88 sets templateDirty_=true and 105-106 `void prepare(...) { if (templateDirty_) rebuild_();` so the loader never forces the build. rebuild_ (134-151) calls builder_ → loader 1036-1038 build_subgraph_with_seed_perturbation → 1143 `json::parse(subtreeJsonStr)` + 1163 build_graph. UI path: mforce_ui/main.cpp:4676 `std::lock_guard<std::mutex> lock(g_audioMutex);` then 4691 `pitched->prepare_voice_at(...)` → instrument.h:349+ `vg.source->prepare(...)`; audio_callback takes the same mutex at 3864.`

Why: Unbounded, allocating work (JSON parse, string copies, ~count×nodes shared_ptr constructions, fprintf) happens while the RtAudio callback is blocked on g_audioMutex — an underrun source on the first note of every voice of a Multiplex patch. The loader already has all the information to build at load time. No patches/library patch uses MultiplexSource today, which is why this is medium rather than high.

Recommendation: Build eagerly: after set_template, call a new `mux->build_now()` (rebuild_ made public, or have set_template build immediately) so prepare() only propagates. For UI mark_dirty rebuilds, rebuild into a staging vector on the UI thread outside the lock and swap under it.


## F276 [UNVERIFIED] high (reporter: high) duplication — engine/src/patch_loader.cpp:1194

**The legacy paramMap exists as three parallel implementations: engine build_bindings, UI JSON converter, and UI stash editor/dialog**

Reporters: arch-hacks-census

Evidence: `patch_loader.cpp:1194 `static void build_bindings(const json& paramMapJson, ...)` (~137 lines, decision matrix cases 1-4);
tools/mforce_ui/main.cpp:1413 `static bool convert_parammap_to_wiring(nlohmann::json& root)` (~147 lines, 'Mirrors build_bindings' matrix');
tools/mforce_ui/main.cpp:7625 `ImGui::TextDisabled("Legacy paramMap entries (not convertible to nodes)");` plus s_loadedParamMap touched at ~40 sites (737, 2155, 2703, 4541, 7086, 7110, 7513, 7798, 9205 mapping_badge, 10569-10708)`

Why: Three code paths implement one retired concept with slightly different semantics: the engine keeps push-delivery (instrument.h:417-419 notes a paramMap patch 'stays instantaneous; only pulled frequency chains glide'), the UI converts most entries to wiring nodes at load, and the remainder lives in a stash with its own editor, badge system and carry-forward on save. Backlog 35 (dsp/BACKLOG.md:118-121) already says the dialog 'should stop accepting new bindings' and that Convert node->patch synthesizes a paramMap row that collides with a Note-face wire. Every loader/UI change must be made three times, and the semantics already diverge (glide).

Recommendation: Finish the migration: convert at load unconditionally (make owned-formant children representable or reject them loudly), delete build_bindings and the UI stash/editor/badge once the gate corpus has no paramMap left, and make the Convert lane emit Note-face wires (backlog 35). Record the remaining exemptions (owned formant children) as a single named debt rather than a live third path.


## F277 [UNVERIFIED] high (reporter: high) duplication — engine/src/patch_loader.cpp:1514

**Instrument assembly is copy-pasted between load_patch_file and load_instrument_patch_json and has already drifted: nodesById is only set on the JSON path, so per-note reseed and capture are silently inert for CLI patch-file renders**

Reporters: render-loader

Evidence: `1514-1580 and 1840-1889 both do: polyphony, make_unique<PitchedInstrument>, volume/onsets/glideSec/sustaining, the 'instrument.release retired' fprintf, then per voice make_perform_context / build_graph / output lookup / Envelope collection / topMultiplex / attach_perform_source / bind_wiring / promote_starved_refs / advanceList. Only 1886 has `vg.nodesById = std::move(g.valueNodes);`. instrument.h:475 `for (auto& [nid, src] : vg.nodesById) src->reseed();` and instrument.h:235 `voicePool[v].nodesById.find(ids[k])` therefore iterate an empty map for instruments built by load_patch_file. Node indexing 1503-1509 vs 1832-1838 is a third copy.`

Why: A 50-line block duplicated once has already produced a behavioral divergence: the same patch rendered via `mforce_cli` patch-file mode gets no per-note draw re-anchoring (onsets-v2 addendum) while the UI/Generate path does. Every future voice-assembly rule (the advance list and allEnvelopes were each added to both copies by hand) doubles the edit and the drift risk.

Recommendation: Extract `static std::unique_ptr<PitchedInstrument> build_instrument(const json& root, const NodeIndex& idx, int minPolyphony)` containing the whole 1514-1580 body (including nodesById); load_patch_file calls it then does score + mixer; load_instrument_patch_json calls it and returns. Also factor the nodeMap/nodeOrder indexing into one `index_nodes(const json& graph)` that throws on duplicate ids.


## F278 [UNVERIFIED] high (reporter: high) duplication — engine/src/patch_loader.cpp:1514

**Patch loader has twin entry points and three copies of the JSON ref scanner**

Reporters: arch-duplication

Evidence: `load_patch_file 1514-1580 vs load_instrument_patch_json 1840-1889 (comments reference "see render-path twin above"); ref scanners at 190-214, 235-255 and 1104-1113; `WaveSourceMono` 58-65 vs `ValueSourceMono` 67-74 are byte-identical adapters.`

Why: ~50 lines of graph-build/instrument-block logic exist twice; every loader fix (starved-ref promotion, tap binding, score v2 fields) must be applied to both or the CLI render and the UI/instrument paths diverge — the 2026-08/09 history in this file shows that already happened several times (RD audition-path mismatch, UI-vs-CLI sound mismatch harnesses).

Recommendation: One `build_patch(const json&, int sr, int minPolyphony) -> InstrumentPatch` core; load_patch_file and load_instrument_patch_json become thin wrappers. One `for_each_ref(json, fn)` walker. Delete WaveSourceMono.


## F279 [UNVERIFIED] medium (reporter: medium) efficiency — engine/src/patch_loader.cpp:1534

**JSON-only analyses are recomputed once per voice (and the advance list twice per voice) although they depend only on the patch JSON**

Reporters: render-loader

Evidence: `Per voice iteration at 1534-1580 (and 1855-1888): 1571 promote_starved_refs rebuilds advancer (259-264) and reachable (271-282) from JSON and calls collect_advance_ids at 273; 1576 calls collect_advance_ids again; 1567 bind_wiring rebuilds the reaches_freq memo (1357); the Multiplex branch re-extracts and re-dumps the subtree per voice (1008-1009 `extract_subgraph_json(...); subtree.dump()`); the unknown-key pass allocates `std::string(src.type_name()) + "." + it.key()` (548-549) per key per node per voice.`

Why: Load time is user-visible on the UI Generate path (load_instrument_patch_json per Generate, main.cpp:4352/4643) and live pools use LIVE_MIN_POLYPHONY-sized voice counts, so this is O(polyphony × nodes × keys) string/map work repeated for identical input. The 466 ms per-note reload that was previously killed shows load latency matters here.

Recommendation: Compute a `GraphAnalysis{advancer, reachable, advanceIds, bendInertIds, muxSubtreeStr}` once per load from nodeMap/nodeOrder and pass it into each voice build; keep the built-graph walk (293-323) per voice since it operates on per-voice objects.


## F280 [UNVERIFIED] medium (reporter: medium) duplication — engine/src/patch_loader.cpp:1534

**Voice-pool construction and instrument-block parsing are duplicated between load_patch_file and load_instrument_patch_json, and have already diverged (nodesById only populated in one)**

Reporters: arch-hacks-census, arch-valuesource-graph

Evidence: `patch_loader.cpp:1534-1580 (load_patch_file): for (int v = 0; v < polyphony; ++v) { PitchedInstrument::VoiceGraph vg; PerformContext perf = make_perform_context(...); auto g = build_graph(...); ... attach_perform_source(instJson, g, vg); bind_wiring(...); promote_starved_refs(...); for (const auto& aid : collect_advance_ids(...)) vg.advanceList.push_back(...); inst->voicePool.push_back(std::move(vg)); }

patch_loader.cpp:1855-1889 (load_instrument_patch_json): identical sequence, plus line 1886: vg.nodesById = std::move(g.valueNodes);

instrument.h:475 (deliver_continuation): for (auto& [nid, src] : vg.nodesById) src->reseed();`

Why: Two copies of a 40-line voice build (and of the 12-line instrument-header parse at 1518-1531 vs 1843-1853) with 'see render-path twin above' comments standing in for a shared function. The divergence is already live: load_patch_file never fills nodesById, so a held line rendered through the embedded-score path (load_patch_file → play_note → deliver_continuation) skips the per-note reseed that the onsets-v2 addendum made mandatory — the CLI smoke render of a hold-marked score produces different noise realisations than the UI/CLI load_instrument_patch path for the same patch and score. Also, instrument.h:161-163 says nodesById is 'Empty for mixer-path instruments' while instrument.h:475 relies on it unconditionally.

Recommendation: Extract `build_voice_pool(nodeMap, nodeOrder, outputId, instJson, sampleRate, polyphony) -> PitchedInstrument` (or at least `build_voice(...) -> VoiceGraph`) and `parse_instrument_block(instJson, PitchedInstrument&)`; have load_patch_file call it then wrap in a mixer. Always populate nodesById (the comment's size concern is ~node-count shared_ptrs per voice). Add an engine_tests case rendering a held line through both entry points and comparing bytes.

Also reported as: Voice-pool construction and instrument-block parsing are duplicated between load_patch_file and load_instrument_patch_json; the JSON ref-scan lambda is written four times


## F281 [UNVERIFIED] low (reporter: low) smell — engine/src/patch_loader.cpp:1585

**load_patch_file (330 lines) embeds score scheduling and then a second pass that re-implements the same 'time defaults to prevEnd' rule to compute duration**

Reporters: render-loader

Evidence: `1602-1648 schedules notes with `float start = noteJson.contains("time") ? noteJson["time"].get<float>() : prevEnd;` and 1659-1665 loops the same array again with `double t = noteJson.contains("time") ? noteJson["time"].get<double>() : prevEnd;` to find maxEnd. CLAUDE.md: the embedded score block is 'a smoke-test convenience only'.`

Why: The loader's job is the graph and instrument; performer logic and two divergent copies of the timing rule (float vs double) are debt for a smoke-test path. Also contributes to load_patch_file being a second god-function next to build_graph.

Recommendation: Fold maxEnd tracking into the scheduling loop (one pass) and move the block to `schedule_embedded_score(PitchedInstrument&, const json& score)` in the render layer; load_patch_file then reads as index → build_instrument → schedule → mixer.


## F282 [UNVERIFIED] medium (reporter: medium) workaround-hack — engine/src/patch_loader.cpp:1687

**Whole node graph is built a second time, with a throwaway PerformContext, only to resolve the mixer's gainL/gainR**

Reporters: arch-hacks-census, render-loader

Evidence: `patch_loader.cpp:1689-1699 `// Build a throwaway graph just to resolve mixer/channel params.
            // It rebuilds the WHOLE node graph, PerformNodes included ...
            PitchedInstrument::VoiceGraph mixThrowaway;
            PerformContext mixPerf = make_perform_context(mixThrowaway, nullptr, sampleRate);
            auto gMix = build_graph(nodeMap, nodeOrder, sampleRate, &mixPerf);``

Why: For every instrument patch that carries a 'mix' node, load cost is (polyphony + 1) full graph builds — partials tables, mesh allocation, multiplex template extraction — to read two scalar/ref params. It also creates a PerformSource bound to a null InstrumentState (`make_perform_context(mixThrowaway, nullptr, ...)`) solely so PerformNode branches do not throw. Undocumented in any backlog item.

Recommendation: Resolve mixer gains from the already-built voice-0 GraphResult (keep `g` from the first voice iteration) or restrict gainL/gainR to numbers/refs resolvable against a minimal cone (walk only the nodes reachable from the mix params).

Also reported as: Instrument-mode mixer gain override builds a throwaway copy of the entire graph, keyed on the magic node id "mix", with a PerformSource holding a null InstrumentState


## F283 [UNVERIFIED] medium (reporter: medium) soundness — engine/src/patch_loader.cpp:1783

**Mixer/channel params resolved after build_graph bypass the usage counter, so a source shared between a graph pin and a channel volume/pan or mixer gain is advanced twice per sample**

Reporters: render-loader

Evidence: `635 `std::unordered_map<std::string, int> usage;` is local to build_graph and not returned in GraphResult. 1783-1784 `ch.volume = resolve_param_or(p, "volume", 1.0f, valueNodes); ch.pan = resolve_param_or(p, "pan", 0.0f, valueNodes);`, 1765-1766 and 1701-1702 (gainL/gainR) all pass usage=nullptr, so resolve_param 118-121 never wraps in RefSource. mixer.cpp:28-31 pulls `gainL->next()`, `ch.volume->next()`, `ch.pan->next()` every sample. The comment at 1806-1807 only acknowledges the cross-channel case.`

Why: This is the same bug the usage counter was introduced to fix (98-103: an envelope wired to N consumers exhausts in 1/N of the note). Any mixer-mode patch that drives a channel volume from an envelope or LFO that also feeds a graph pin renders with that modulator running at double rate.

Recommendation: Return `usage` in GraphResult (or keep a `WireContext{valueNodes, usage, tapBinds}` struct that resolve_param takes by reference — this also retires the thread_local t_tapBinds at 89) and pass it to every post-build resolve_param_or call.


## F284 [UNVERIFIED] high (reporter: high) soundness — engine/src/red_noise_source.cpp:10

**Constructor/fallback defaults disagree with descriptor defaults in three classes, so the same node sounds different from CLI JSON vs the UI**

Reporters: sources-noise

Evidence: `red_noise_source.cpp:10-12: density_=ConstantSource(1.0f); smoothness_=ConstantSource(1.0f); rampVariation_=ConstantSource(1.0f);  vs red_noise_source.h:34-36: {"density", 0.5f…}, {"smoothness", 0.5f…}, {"rampVariation", 0.0f…}.  wander_noise_source.h:198-199: float mnSpd = minSpeed_ ? … : 1.0f; float mxSpd = maxSpeed_ ? … : 1.0f;  vs lines 154-155: {"minSpeed", 0.01f…}, {"maxSpeed", 0.1f…}.  layered_red_noise_source.h:21-22: frequencies_ = {0.5f, 2.0f, 8.0f}; amplitudes_ = {1.0f, 1.0f, 1.0f};  vs lines 38-39 array defaults 7.0f / 0.05f.`

Why: Verified both consumers: the patch loader only wires params present in JSON (patch_loader.cpp:377-381 `if (!params.contains(desc.name)) continue;`), so an omitted key keeps the constructor value; the UI creates every pin from desc.default_value and set_param's a ConstantSource for it (main.cpp:614-615, 108-111, 463-465). A RedNoiseSource with no explicit density/smoothness/rampVariation is therefore 1.0/1.0/1.0 via mforce_cli and 0.5/0.5/0.0 via mforce_ui — the exact 'audition-path mismatch' class of bug already logged for the algev patches. WanderNoise2 with pins omitted steps at 1.0 per sample instead of 0.01-0.1 (rail-to-rail noise instead of a walk). Wander's nullable-pin style (null checks at 60-63, 67-73, 182-202, 295-314) is what makes a second set of fallback constants possible at all.

Recommendation: Make the descriptor table the single source of truth: construct every pin as ConstantSource(desc.default_value) (drop the Wander null-pin style and its 20+ per-sample branches), and initialise LayeredRedNoise rows from the ArrayDescriptor defaults. A one-line loader fallback (wire ConstantSource(desc.default_value) for absent keys) would also close the CLI/UI gap independently of per-class fixes.


## F285 [UNVERIFIED] medium (reporter: medium) soundness — engine/src/red_noise_source.cpp:20

**prepare() leaves ramp/walk state from the previous note in RedNoise, WhiteNoise, WanderNoise and WanderNoise2, unlike their siblings**

Reporters: sources-noise

Evidence: `red_noise_source.cpp:20-28 prepare() only prepares the six pins; rampSize_/sampleCount_/lastValue_/nextValue_/zeroRamp_/lastSign_ (red_noise_source.h:79-84) are untouched, so line 43 `if (sampleCount_ >= rampSize_ || …)` on the first sample of the next note tests stale values. white_noise_source.h:67-73 and wander_noise_source.h:59-64, 182-189 likewise. Counter-examples in the same unit: pink_noise_source.h:49-55 resets index_/runningSum_/rows_, wander_noise_source.h:294-303 resets value_/slope_/slopeChgDir_.`

Why: Pool voices are reused round-robin (instrument.h:541-543) and prepare()d per note (instrument.h:607), so the second note on a slot resumes a RedNoise ramp mid-flight from the previous note's last/next values, and the continuity/zeroCross paths of WhiteNoise start from the old note's lastVal_/lastSign_. instrument.h:469-470 asserts 'prepare() already seeds' as the reason reseed() is skipped on the fresh path — that does not hold for these sources (neither state nor rng_ is re-anchored in prepare), so first-note determinism on a reused slot is not what the design claims.

Recommendation: Reset derived state in prepare() for all four (RedNoise: sampleCount_=0, rampSize_=1, lastValue_=nextValue_=0, zeroRamp_=false, lastSign_=-1; White: lastVal_=0, lastSign_=-1; Wander: slope_=0, direction_=1, lastVal_=0.5; Wander2: direction_=1, value_=0). Whether prepare() should also rng_.reanchor() is a cross-unit design call (it would make every fresh note identical); flag to the orchestrator rather than deciding here.


## F286 [UNVERIFIED] low (reporter: low) elegance — engine/src/source_registrations.cpp:64

**~75 factory lambdas are one of four shapes and could be a table; source_registry.h pulls the full nlohmann/json.hpp into a core header**

Reporters: render-loader

Evidence: `Shapes: `[](int sr, auto) { return std::make_shared<T>(sr); }` (64-77, 418-453…), `[](int, auto) { ... <T>(); }` (95-96, 194-195…), `[](int, auto seed) { ... <T>(seed.value_or(K)); }` (110-154…), `[](int sr, auto seed) { ... <T>(sr, seed.value_or(K)); }` (121-129…). source_registry.h:3 `#include <nlohmann/json.hpp>`.`

Why: Not a reflection concern — the list stays explicit — but 400 lines of near-identical lambdas hide the 10 registrations that actually carry a configurator. json.hpp in a core header is ~25k lines for every TU that only needs the registry's create/has.

Recommendation: Add `template<class T> SourceFactory factory_for(uint32_t defaultSeed = 0)` using `if constexpr (std::is_constructible_v<T, int, uint32_t>)` / `(int)` / `(uint32_t)` / `()` and reduce plain registrations to `reg.register_type("SineSource", Oscillator, factory_for<SineSource>());`. Include <nlohmann/json_fwd.hpp> in source_registry.h.


## F287 [UNVERIFIED] medium (reporter: medium) soundness — engine/src/wav_reader.cpp:74

**Chunk walk mis-handles data-before-fmt, odd-length chunk padding, and the 0xFFFFFFFF data size used by streaming-written WAVs (bad_alloc out of a bool API)**

Reporters: render-instrument

Evidence: ``} else if (std::strcmp(tag, "data") == 0) { dataBytes = chunkSize; dataStart = f.tellg(); gotData = true; }` (74-77) — loop continues without skipping the payload, so if fmt follows data the next tag read lands in sample bytes; `f.seekg(chunkSize, std::ios::cur);` (81) ignores the RIFF word-alignment pad byte for odd-sized chunks; `int numSamples = int(dataBytes / 2); out.resize(size_t(numSamples));` (94-95) — dataBytes = 0xFFFFFFFF gives numSamples = 2147483647 and an ~8 GB resize that throws std::bad_alloc from a function whose contract is 'returns false on any parse error'. Caller audition_load_at (main.cpp 5392) has no try/catch.`

Why: The header limits the contract to files written by write_wav_16le_stereo, but the UI exposes a folder browser over renders/ and Matt's archaeology WAVs (SAVIHost/AF captures) carry LIST/INFO chunks and, for streaming captures, the 0xFFFFFFFF placeholder size. The odd-pad case silently fails to parse; the placeholder case terminates the UI.

Recommendation: In the data branch `f.seekg(dataBytes + (dataBytes & 1), cur)` when fmt is not yet seen (and keep dataStart); pad unknown-chunk skips by `(chunkSize & 1)`; clamp dataBytes to the remaining file size (seek to end once to get it) so the resize is bounded and the function returns false instead of throwing.


## F288 [UNVERIFIED] medium (reporter: medium) efficiency — engine/src/wav_reader.cpp:96

**Reader and writer both do per-sample 2-byte stream I/O instead of one bulk transfer**

Reporters: render-instrument

Evidence: `wav_reader.cpp 96-99: `for (int i = 0; i < numSamples; ++i) { uint8_t b[2]; f.read(reinterpret_cast<char*>(b), 2); ...` ; wav_writer.cpp 56-60: `for (float s : interleavedLR) { ... f.put(char(v & 0xFF)); f.put(char((v >> 8) & 0xFF)); }``

Why: A 60 s stereo file is 5.76 M `read`/`put` calls, each paying the iostream sentry/locale path — the writer runs on every render and the reader on every audition-folder load in the UI (main.cpp 5392), where it stalls the UI thread.

Recommendation: Reader: `std::vector<uint8_t> raw(dataBytes); f.read(raw.data(), dataBytes);` then convert with `std::bit_cast`/shift. Writer: pack into `std::vector<int16_t>` (little-endian host, or byte-pack explicitly) and issue one `f.write`.


## F289 [UNVERIFIED] medium (reporter: medium) soundness — engine/src/wav_writer.cpp:56

**write_wav_16le_stereo returns true without ever checking the stream after writing**

Reporters: render-instrument

Evidence: ``for (float s : interleavedLR) { int16_t v = clip(s); f.put(char(v & 0xFF)); f.put(char((v >> 8) & 0xFF)); } return true;` — the only failure check is `if (!f) return false;` at open (30).`

Why: A disk-full, quota or sharing-violation error mid-write leaves a truncated WAV while every CLI/UI caller (mforce_cli main.cpp 110/191/331/623/726/786/892/1010, explore.cpp 383, mforce_ui 8534/13084/13308/13432) reports success. Renders are the product; a 'render succeeded' that is silently truncated is a release-embarrassing failure mode.

Recommendation: Write the sample block from a packed buffer with one `f.write`, then `f.flush(); return f.good();` (or `return static_cast<bool>(f);`).


## F290 [UNVERIFIED] high (reporter: high) rt-safety — engine/src/wavetable_source.cpp:21

**fill_table() throws std::runtime_error from inside next(); the live audio callback has no exception handler**

Reporters: sources-evolution

Evidence: `if (currFreq_ <= 0.0f || float(sampleRate_) / currFreq_ < 1.0f) { throw std::runtime_error("WavetableSource: invalid frequency for table fill"); }`

Why: compute_raw/compute_interpolated call fill_table at ptr_==0 (lines 44-46, 100-101); next() is pulled from audio_callback (tools/mforce_ui/main.cpp:3857-3914), which I read end to end — there is no try/catch, so an exception propagates out of the RtAudio thread and std::terminate()s the UI. The f <= 0 half is unreachable (WaveSource::next already throws at dsp_wave_source.h:38 — same problem, core unit), but f > sampleRate is reachable from any wired modulator on the frequency pin (pins are not clamped to the 20000 descriptor max). Exceptions are not a real-time-safe error path.

Recommendation: Never throw from next(): clamp len to >= 2 (or emit silence and set a latched error flag the UI can display). Apply the same to WaveSource::next.


## F291 [UNVERIFIED] high (reporter: high) rt-safety — engine/src/wavetable_source.cpp:27

**Table allocation deferred to the first next(): WavetableSource, HybridKSSource and BasicAdditiveSource resize vectors inside the sample path**

Reporters: arch-modern-cpp

Evidence: `27: `values_.resize(len);` in fill_table(), called at 46 from compute_raw (first next()); engine/src/hybrid_ks_source.cpp:35 `table_.resize(len);` and 52-54 `partialAmpl_.resize(numPartials_); partialPhase_.resize(...); startAmpl_.resize(...)`; engine/include/mforce/source/additive/basic_additive_source.h:121 `partialPos_.resize(count, 0.0f);` inside compute_wave_value`

Why: These are heap allocations on the render thread, violating the project's hot-loop rule. In the UI they run under the audio mutex at note-on; in RepeatingSource-driven graphs they recur every repetition.

Recommendation: Size tables in prepare() (or at set_param time when the frequency is known) and treat next() as allocation-free; assert capacity in debug builds.


## F292 [UNVERIFIED] medium (reporter: medium) duplication — engine/src/wavetable_source.cpp:44

**First-sample init and the tuning allpass are duplicated across compute_raw/compute_interpolated (and the allpass a third time in EKS); the interpolated copy is incomplete**

Reporters: sources-evolution

Evidence: `compute_raw 44-63: fill_table; adjust; shape_excitation; frac/tuningCoeff_; tuningState_=0; tuningPrevIn_=0; tablePtr2_=-1; readPos_=-1  — compute_interpolated 100-115: same block minus baseFreq_/tablePtr2_/readPos_.  Allpass `y = C*out + prevIn - C*state` at 89-94, 135-140 and wave_evolution.h:326-328.`

Why: Two copies of note-start logic already diverged: compute_interpolated does not reset baseFreq_, tablePtr2_ or readPos_. `interpolate` is a live setting (wavetable_source.h:74-76, applied under lock from the UI), so toggling it mid-note switches into a path whose state is stale from the other mode. Also, speedFactor is declared as a param (header line 35) but only pulled in interpolated mode (cpp line 118) — a wired envelope on it is silently inert in the default raw mode.

Recommendation: Extract `begin_note()` called from both paths, and a 6-line `Allpass1 {float c, x1, y1; float process(float)}` reused by the tuning stage and EKS stiffness. Either pull speedFactor in both modes or drop it from raw mode's descriptor.


## F293 [UNVERIFIED] medium (reporter: medium) soundness — engine/src/wavetable_source.cpp:120

**Evolutions assume evolve() sweeps indices sequentially once per sample; the interpolated readout path with speedFactor != 1 skips or repeats indices and misses the index==0 cycle triggers**

Reporters: sources-evolution

Evidence: `float tablePtr = float(len) * currPos_ * sf; int samp1 = int(std::floor(tablePtr)); samp1 = samp1 % len; ... if (evolution_) { evolution_->evolve(values_, samp1); }`

Why: ReactionDiffusion (1035 `if (index == 0) { uPrev_ = u_; ...}`), CellularAutomaton (1225), HistogramEqualize (1310 rebuild_cdf) and Target's cycle counter (206-210, assumes one call per table slot) all key on the evolve index. With sf > 1 the read head steps ~2 slots per sample, so half the table is never evolved and index 0 is hit on some cycles and not others — snapshots and the CDF go stale for multiple periods. In raw mode WavetableSource correctly separates the evolve clock (tablePtr2_, line 66) from the read head; interpolated mode conflates them.

Recommendation: Drive evolve() from the sequential tablePtr2_ clock in both modes; the read position (integer or fractional, speed-scaled or bent) is a separate concern that already works that way in compute_raw.


## F294 [UNVERIFIED] low (reporter: low) smell — research/additive_perf/simd_proto.cpp:85

**Prototype hygiene: unused 'base' parameter with a stale comment, a scalar tail that never runs and is not the faithful control it claims, and AVX2/FMA intrinsics with no CPU check**

Reporters: tools-misc-build

Evidence: `void build(int count, float base) { ... (void)base; } under a comment 'base tuned so a realistic fraction of partials sits past the 16 kHz cutoff' (105-107); run_simd's scalar tail 'for (int i = nv; i < n; ++i)' (292-306) is dead for every tested count ('const int counts[] = {32, 96, 200, 512};' at 327, all multiples of 8) and multiplies in a different association from run_scalar:134-137; _mm256_fmadd_ps (71, 248) is emitted with no __cpuid guard.`

Why: This is a closed research artifact (anti-results recorded in the spec; partials.h:687 cites it) and the binaries are gitignored, so the cost is only to the next reader who tries to re-run or extend it. Noting it so the orchestrator can decide whether research/ is in release scope at all.

Recommendation: Either leave as an archived measurement with a header line saying 'closed, see spec' or, if kept live, drop the base parameter, make the tail share the exact scalar body, and gate on __cpuid AVX2+FMA bits.


## F295 [UNVERIFIED] low (reporter: low) build — tools/durn_converter/CMakeLists.txt:4

**Target pins C++17 under a C++20 project and adds an engine include directory nothing uses**

Reporters: tools-durn

Evidence: `CMakeLists.txt:10 `set_target_properties(durn_converter PROPERTIES CXX_STANDARD 17)` versus root CMakeLists.txt:4-5 `set(CMAKE_CXX_STANDARD 20) set(CMAKE_CXX_STANDARD_REQUIRED ON)`. CMakeLists.txt:4 `target_include_directories(durn_converter PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)` while line 3 says "Header-only — no library dependency needed" and no file in the unit includes anything outside parsers/ and the standard library (all four headers state "no engine dependencies").`

Why: The tool cannot use C++20 facilities the rest of the codebase uses (std::span, designated initializers, ranges) and the stray include path is a dormant coupling that will surprise whoever adds an engine include later.

Recommendation: Drop the CXX_STANDARD override (inherit 20) and remove the unused include directory; if the engine's dun_parser.h is ever shared for round-trip validation, add it then.


## F296 [UNVERIFIED] medium (reporter: medium) build — tools/durn_converter/CMakeLists.txt:4

**Two tools pin C++17 while pointing at the C++20 engine include tree they do not actually use**

Reporters: arch-build-headers

Evidence: `tools/durn_converter/CMakeLists.txt:3-4 `# Header-only — no library dependency needed (uses research parsers directly)` / `target_include_directories(durn_converter PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)` and :10 `set_target_properties(durn_converter PROPERTIES CXX_STANDARD 17)`; tools/ppl_to_json/CMakeLists.txt:4-7 adds engine/include and engine/third_party, :13 `CXX_STANDARD 17`. Verified neither main.cpp includes any mforce/ header (durn_converter/main.cpp:9-18 includes only parsers/* and STL; ppl_to_json/main.cpp:11-19 only nlohmann + STL); 47 engine headers use std::span.`

Why: The include paths are vestigial and the standard pin is a trap: the first `#include "mforce/..."` added to either tool fails on std::span with errors that look like engine bugs. A mixed-standard tree also blocks a global `CMAKE_CXX_EXTENSIONS OFF`/`cxx_std_20` policy.

Recommendation: Remove the engine include directories and the CXX_STANDARD 17 overrides from both tools (they compile fine at the project's C++20), or if a tool truly must stay C++17 for an external reason, drop the engine include path so the boundary is explicit.


## F297 [UNVERIFIED] medium (reporter: medium) soundness — tools/durn_converter/main.cpp:25

**emit_durn silently quantizes durations to ten tokens and re-synthesizes bar/phrase structure from accumulated beats, although DURN supports triplets and ties and the sources carry real barlines**

Reporters: tools-durn

Evidence: `main.cpp:28-42 `static const DurMap map[] = { {"w.", 6.0}, ... {"s", 0.25} }; ... if (diff < best_diff) { best_diff = diff; best = d.tok; }` with no threshold or warning, so a triplet eighth (0.333) emits as 's' (0.25) and any merged tie > 6 beats emits as 'w.'. The engine reader accepts a `3` triplet prefix (dun_parser.h:66-70, 94) and `T`/`t` tie tokens (dun_parser.h:114-116, 278-291) that this emitter never produces. Bars are then inferred: main.cpp:98-102 `beat_in_bar += u.duration; if (beat_in_bar >= beats_per_bar - 0.01) { bar_count++; beat_in_bar = std::fmod(beat_in_bar, beats_per_bar);` and phrases hard-coded every four bars at :104 `(bar_count % 4 == 0)`. All parsers discard source barlines: abc_parser.h:306-310 skips '|', kern_parser.h:437 `if (line[0] == '=') continue;`, mxml flattens <measure>s (musicxml_parser.h:473-518).`

Why: Quantization error compounds through the bar accumulator, so after one triplet group or one cross-bar tie every subsequent '|' and '/' lands in the wrong place, and the corpus's figure/phrase segmentation (the thing the Markov work consumes) is wrong from that point on with no diagnostic.

Recommendation: Emit the `3` prefix when a duration is within epsilon of 2/3 of a token value; split durations longer than the longest token into tied tokens; emit a stderr warning (or fail) when the residual exceeds epsilon. Keep source bar indices in the IR and place '|' from them rather than from accumulated beats; make the 4-bar phrase rule a CLI option rather than a constant.


## F298 [UNVERIFIED] high (reporter: high) duplication — tools/durn_converter/main.cpp:59

**No common intermediate representation: four parsers each own a FigureUnit, a ScaleMap, a tie-merger and a letter table; their conversion layers are dead, discarded or lossy, and main.cpp re-derives everything in four bespoke adapters**

Reporters: tools-durn

Evidence: `Five unit structs: `struct GenericUnit { float duration; int step; bool is_rest; int accidental{0}; }` (main.cpp:59-64), abc::FigureUnit (abc_parser.h:154, double), kern::FigureUnit (kern_parser.h:40, float), midi::FigureUnit (midi_parser.h:56), mxml::FigureUnit (musicxml_parser.h:262). Four pitch→degree implementations each with its own floor-division: abc::KeySignature::semitone_to_scale_degree + octave fixup (abc_parser.h:124-138, 533-535), kern::ScaleMap::pitch_to_degree while-loops (kern_parser.h:95-96), midi::ScaleMap::pitch_to_degree `octave = -(((-semi - 1) / span) + 1)` (midi_parser.h:87), mxml::ScaleMap::midi_to_degree `octave = (semitones_from_root - 11) / 12` (musicxml_parser.h:319). Three letter→semitone switches (abc_parser.h:21-32, kern_parser.h:141-152, musicxml_parser.h:238-247). Three tie mergers (abc merge_tied 465-492, kern merge_ties 530-544, mxml merge_ties 545-572). Four pitch-class name tables in main.cpp alone (372, 456, 457, 533-537). Of the parsers' own conversion layers: midi::to_figure_units (midi_parser.h:507) and mxml::convert_to_figure_units (578) have no callers; kern's `cr.units` from convert_file (main.cpp:225) is never read; abc's is used but lossy (previous finding).`

Why: Every bug fix (accidental handling, octave wrap, tie semantics, rest representation) must be made four times and currently is not: ABC drops accidentals, MusicXML drops rests, kern reports a chromatic remainder that is always non-negative (kern_parser.h:107, so Eb is spelled D+ while ABC would spell it D natural). The 'FigureUnit' abstraction in each parser does not earn its keep because it throws away is_rest and accidental, forcing main.cpp to reach back into format-specific note lists by parallel index (main.cpp:214 `tune.notes[i].is_rest`).

Recommendation: Define one `durn::SourceNote { double beats; int midi; bool rest; }` plus `SourceScore { key_root_pc, minor/mode, meter, bpm, notes, bar_boundaries }`. Each parser emits only that (absolute pitch, no degree math). One shared ScaleMap (take midi_parser's as the base, add a signed chromatic_remainder) and one adapter compute step/accidental/start-degree for all formats, then emit_durn. Delete the four per-parser FigureUnit/to_figure_units layers and the dead ScaleMap factories.


## F299 [UNVERIFIED] medium (reporter: medium) workaround-hack — tools/durn_converter/main.cpp:176

**ABC pickup detection re-scans the raw text behind the parser's back and miscounts chord symbols and grace notes**

Reporters: tools-durn

Evidence: `main.cpp:189-196 `int notesBeforeBar = 0; for (size_t p = bodyStart; p < rawText.size(); ++p) { char ch = rawText[p]; if (ch == '|') break; if ((ch >= 'a' && ch <= 'g') || (ch >= 'A' && ch <= 'G') || ch == 'z' || ch == 'Z') notesBeforeBar++; }` then :199-200 sums `tune.notes[i].duration_beats` for the first notesBeforeBar parsed notes. The scan has no awareness of `"..."` chord annotations or `{...}` grace groups, which parse_body explicitly skips (abc_parser.h:313-325), so a leading `"Am"` or `{g}` before the first barline adds phantom letters and the pickup sum absorbs one or two extra real notes.`

Why: Wrong pickup length shifts the first '|' and therefore every bar boundary of the tune (emit_durn, main.cpp:87-96). The need for this hack exists only because the parser discards barlines; fixing it in place keeps two notion of 'note' in sync by hand.

Recommendation: Record barline positions (or a per-note bar index) in AbcTune during parse_body and derive pickup as the beat sum before the first barline; delete the raw-text rescan.


## F300 [UNVERIFIED] high (reporter: high) soundness — tools/durn_converter/main.cpp:211

**ABC chromatic notes are silently re-pitched to the nearest diatonic degree with no accidental emitted**

Reporters: tools-durn

Evidence: `main.cpp:213-215 `for (...) { bool rest = ...; units.push_back({float(seq.units[i].duration), seq.units[i].step, rest}); }` — GenericUnit.accidental is left at 0. abc::FigureUnit (abc_parser.h:154-157) has only {duration, step}; abc::to_figure_units (abc_parser.h:526-530) folds the accidental into abs_semi and then `key.semitone_to_scale_degree(rel_semi)` (abc_parser.h:124-138) snaps to the nearest degree with ties resolved to the lower index. So in C major `_B` (semitone 10, equidistant from A=9 and B=11) becomes degree A with no '-' suffix, and `^F` becomes F natural. The kern (main.cpp:284-287) and MIDI (main.cpp:398-407) adapters do emit accidentals via step_suffix (main.cpp:51-52), and dun_parser.h:161 consumes them.`

Why: Every chromatic note in every ABC source is written to the corpus at a different pitch than the source, with no warning. For a melody corpus feeding Markov/figure generation this is silent data corruption of exactly the notes (leading tones, blue notes, modal mixture) that carry stylistic signal.

Recommendation: Compute the chromatic remainder for ABC the same way the kern/MIDI adapters do (abs_semi minus the semitone of the chosen degree) and pass it as GenericUnit.accidental. Long-term this disappears when one shared ScaleMap produces (degree, accidental) for all four formats.


## F301 [UNVERIFIED] medium (reporter: medium) efficiency — tools/durn_converter/main.cpp:224

**kern path parses and tie-merges every file twice; the first conversion's output is thrown away**

Reporters: tools-durn

Evidence: `main.cpp:225 `auto cr = kern::convert_file(path, kern::VoiceSelect::Last, kern::RestHandling::AsZeroStep);` (which internally runs parse_file + merge_ties + to_figure_units, kern_parser.h:638-644) — only `cr.metadata` is read (main.cpp:232-248). Then main.cpp:251-252 `auto kf = kern::parse_file(path, kern::VoiceSelect::Last); auto merged = kern::merge_ties(kf.melody);` repeats the parse and merge, and `kf.metadata` carries the same key/meter as `cr.metadata`.`

Why: Double file I/O and double parse per .krn file, plus a dead FigureUnit conversion, purely because main.cpp needs is_rest/accidental that kern::FigureUnit does not carry. It is the clearest symptom of the missing shared IR.

Recommendation: Call parse_file once, use kf.metadata for the header, and delete the convert_file call (and, once the shared adapter exists, kern::to_figure_units/ConversionResult entirely).


## F302 [UNVERIFIED] medium (reporter: medium) duplication — tools/durn_converter/main.cpp:391

**midi_to_durn copy-pastes a 14-line accidental block into both branches and re-implements ScaleMap's nearest-degree search**

Reporters: tools-durn

Evidence: `main.cpp:393-407 and :417-430 are character-identical: `int semiInOctave = ((n.pitch - rootPitch) % 12 + 12) % 12; bool isDiatonic = false; for (int s : scale.semitone_offsets) { if (s == semiInOctave) { isDiatonic = true; break; } } int acc = 0; if (!isDiatonic) { int nearestSemi = scale.semitone_offsets[0]; for (int s : scale.semitone_offsets) { if (std::abs(s - semiInOctave) < std::abs(nearestSemi - semiInOctave)) nearestSemi = s; } acc = semiInOctave - nearestSemi; }`. The nearest search duplicates midi::ScaleMap::pitch_to_degree (midi_parser.h:92-100), which already finds best_idx but discards the remainder.`

Why: Two copies of non-trivial logic inside one function invite divergence; the only difference between the branches is `step = 0` vs `curDeg - prevDeg`.

Recommendation: Hoist the accidental computation into a helper (or return the signed remainder from ScaleMap::pitch_to_degree) and collapse the if/else to `int step = first ? 0 : curDeg - prevDeg;`.


## F303 [UNVERIFIED] high (reporter: high) soundness — tools/durn_converter/main.cpp:449

**MusicXML conversion is dead: figureUnits is never populated, so every .xml file throws 'No notes found'**

Reporters: tools-durn

Evidence: `main.cpp:450-453 `auto result = mxml::parse_file(path); if (result.figureUnits.empty()) throw std::runtime_error("No notes found in MusicXML file");` — mxml::parse() (musicxml_parser.h:452-521) only fills result.notes; mxml::parse_file (527-537) just calls parse(). The only function that fills figureUnits, `inline void convert_to_figure_units(ParseResult& result)` (musicxml_parser.h:578), has zero call sites repo-wide (grep). Even if wired, main.cpp:484 `gunits.push_back({fu.duration, fu.step, false});` marks every unit non-rest with no accidental, so rests would emit as 'n' repeats.`

Why: One of the four advertised input formats cannot produce a single DURN file; the usage text and the dispatch at main.cpp:603 claim support. A corpus run over .xml silently reports every file as FAIL with a misleading message, and an unopenable file is reported the same way because mxml signals open errors via a sentinel title (musicxml_parser.h:531) that main never inspects.

Recommendation: Have mxml::parse_file (or the main adapter) call convert_to_figure_units, or better, drop mxml::FigureUnit and build GenericUnit directly from the merged MxmlNote list so is_rest and accidental are preserved (see the shared-IR finding). Make parse_file report open failure as an error, not a title string.


## F304 [UNVERIFIED] low (reporter: low) modern-cpp — tools/durn_converter/main.cpp:569

**Grouped nits: unsafe ::tolower, redundant whole-file read, unchecked ofstream, exit code ignores failures, raw FILE*, mixed include guards, uninitialized struct members**

Reporters: tools-durn

Evidence: `main.cpp:569 `std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);` on signed char (abc::detail::to_lower at abc_parser.h:178-183 does the unsigned-char cast correctly). main.cpp:584-587 reads every file into `content` under the comment "Read ABC file" though kern/MIDI/MusicXML then reopen by path (:600-604). main.cpp:607-608 `std::ofstream of(outFile); of << durn;` with no is_open check. main.cpp:620 `return 0;` after counting `failed`. main.cpp:389 `gunits.push_back({float(gap), 0, true, false});` passes bool for the int accidental field. midi_parser.h:383-401 raw `std::FILE* f = std::fopen(...)` with three manual fclose paths and a `std::vector<uint8_t> buf(...)` allocation between open and close. midi_parser.h:1,5 and musicxml_parser.h:1,8 use both `#pragma once` and an include guard. abc_parser.h:38-45 AbcNote has no default member initializers (KernNote at kern_parser.h:21-30 does). abc_parser.h:329-340 dead `if (c == '!' || c == 'H' || c == 'L' || c == 'T')` whose H/L/T arms do nothing and whose comment ends "Actually only process A-G/a-g". kern_parser.h:245 `if ((lower && ch == letter) || (!lower && ch == letter))` is `ch == letter`. kern_parser.h:282-291 a ~40-character whitelist deciding when to stop the accidental scan.`

Why: Individually trivial; together they signal the unit was assembled quickly and never passed a cleanup pass, and two of them (exit code, unchecked ofstream) can hide failures in batch runs.

Recommendation: Fix in one pass: lambda with unsigned char for tolower, read the file only in the ABC branch, check the ofstream, return non-zero when failed > 0, RAII the FILE* (or use ifstream like the other three), one include-guard style, default-initialize AbcNote, delete the dead H/L/T arm, simplify the kern letter test and replace the blacklist with a positive 'is accidental char' test.


## F305 [UNVERIFIED] low (reporter: low) soundness — tools/durn_converter/parsers/abc_parser.h:302

**ABC inline fields and chord brackets are not parsed: `[K:G]` emits a spurious G note, `[CEG]` emits three sequential notes**

Reporters: tools-durn

Evidence: `abc_parser.h:306-310 `if (c == ' ' || ... || c == '|' || c == ':' || c == '[' || c == ']') { ++i; continue; }` skips the delimiters individually; 'K' is then dropped at :374-377 as an unrecognized character, ':' is skipped, and 'G' reaches the note path at :379 with the full default duration. The comment at :308 ("Handle [| |] || etc. -- just skip") shows only barline variants were considered.`

Why: Inline key/meter changes and chord brackets are common in real-world ABC; each produces phantom notes that also shift the bar accumulator in emit_durn.

Recommendation: Treat `[X:...]` as an inline field (apply K:/M:/L: changes) and `[...]` chord groups as a single event (take top or bottom note).


## F306 [UNVERIFIED] medium (reporter: medium) duplication — tools/durn_converter/parsers/kern_parser.h:40

**durn_converter's four format parsers each carry a private ScaleMap/FigureUnit/trim implementation**

Reporters: arch-duplication

Evidence: `ScaleMap/FigureUnit types at kern_parser.h:40-116, midi_parser.h:56-129, abc_parser.h:47-139 and 154-157, musicxml_parser.h:262-339; trim helpers kern 170, abc 171, musicxml 112-115 and ppl_to_json/main.cpp:28.`

Why: ~300 lines of intermediate-representation code duplicated four ways; a fix to the scale map (the figure-level unit the comp lane consumes) must be made in four headers or formats silently diverge.

Recommendation: tools/durn_converter/parsers/common.h with one ScaleMap/FigureUnit/trim; parsers emit into it.


## F307 [UNVERIFIED] medium (reporter: medium) soundness — tools/durn_converter/parsers/kern_parser.h:452

**Spine manipulators (*^ *v *- *x) are not handled, so chosen_spine indexes the wrong column after any spine split**

Reporters: tools-durn

Evidence: `chosen_spine is fixed once at kern_parser.h:452-455 `if (voice == VoiceSelect::First) chosen_spine = kern_spines.front(); else chosen_spine = kern_spines.back();`. Every later interpretation line is consumed by :461-479 `if (line[0] == '*') { ... parse_key_interp / *M ... continue; }` with no branch for '^', 'v', '-' or 'x'; data lines are then read at :484-487 `auto cols = detail::split_tabs(line); ... std::string cell = detail::trim(cols[chosen_spine]);`.`

Why: Polyphonic kern (Bach chorales, keyboard works) routinely splits spines; after `*^` the column count changes and the extractor silently reads a different voice (or a `.` null column) for the rest of the piece with no diagnostic.

Recommendation: Track a per-column spine map and update it on `*^`/`*v`/`*-`/`*x` lines (or at minimum detect them and throw), so VoiceSelect::Last keeps following the same logical spine.


## F308 [UNVERIFIED] low (reporter: low) smell — tools/durn_converter/parsers/midi_parser.h:505

**Dead code and stale comments across the parsers**

Reporters: tools-durn

Evidence: `midi_parser.h:507 `inline std::vector<FigureUnit> to_figure_units(...)` — no callers; its header comment :505 says "step = -1 (sentinel for rest)" but :529 pushes `{static_cast<float>(gap), 0}`. midi_parser.h:120-128 harmonic_minor/chromatic/pentatonic_major — no callers. musicxml_parser.h:353-378 keyDescription/timeDescription — no callers (main.cpp has its own tables at 456-457). kern_parser.h:126-133 ConversionResult counters: `rests_skipped` is incremented for AsZeroStep too (:603 `++cr.rests_skipped;` after pushing the unit). musicxml_parser.h:272 `int rootMidi{0}; // MIDI note of the scale root (e.g. 60 for C4)` but from_key stores a pitch class (:293 `sm.rootMidi = root_pc; // pitch class only (0-11)`). midi_parser.h:258-268 and :358-368 are the same 10-line pending-note flush, with a third near-copy at :333-339.`

Why: Roughly 150 lines that cannot be exercised, plus comments that contradict the code they annotate, raise the cost of the shared-IR refactor and mislead readers about what is live.

Recommendation: Delete the uncalled functions and factories (they return once a shared ScaleMap exists), fix or remove the stale comments, and factor the pending-note flush into a lambda.


## F309 [UNVERIFIED] medium (reporter: medium) workaround-hack — tools/durn_converter/parsers/musicxml_parser.h:26

**Hand-rolled XML by substring search, with a dead get_attribute whose logic is re-implemented inline**

Reporters: tools-durn

Evidence: `find_element (musicxml_parser.h:26-59) returns the slice between the first `<tag` and the first following `</tag>` with no nesting, comment, CDATA or entity handling; `auto content_start = xml.find('>', pos);` (:35) breaks on any attribute value containing '>'. `inline std::string get_attribute(...)` (:132-180) has no call sites (grep), while parse_note_element re-implements attribute extraction by hand: :433-438 `auto type_pos = t.find("type=\""); ... auto val_start = type_pos + 6; auto val_end = t.find('"', val_start);` and only handles double quotes.`

Why: The approach is brittle against real exporter output (comments, single-quoted attributes, entities in titles) and the dead helper is 50 lines of maintenance surface. No XML library exists in engine/third_party today, so this is also a dependency decision rather than an oversight.

Recommendation: Either vendor a tiny XML reader (pugixml is header-plus-one-cpp and MIT) or, if staying hand-rolled, delete get_attribute or use it from parse_note_element, and document the subset of MusicXML the scanner is guaranteed to handle.


## F310 [UNVERIFIED] medium (reporter: medium) smell — tools/durn_converter/parsers/musicxml_parser.h:527

**Four different error conventions across the four parsers; one (MusicXML) uses a sentinel title string main.cpp never inspects**

Reporters: tools-durn

Evidence: `mxml: musicxml_parser.h:530-531 `ParseResult empty; empty.title = "[Error: could not open " + filepath + "]"; return empty;`. midi: `std::string error; // empty on success` (midi_parser.h:53) checked at main.cpp:302. kern: `throw std::runtime_error("kern::parse_file: cannot open " + path);` (kern_parser.h:404). abc: no open check at all and unguarded `std::stoi` (abc_parser.h:252, 221-223) propagates std::invalid_argument on a malformed X:/M:/L: header.`

Why: main.cpp's single try/catch (main.cpp:583-615) only sees two of the four conventions; an unopenable .xml is reported as 'No notes found', and a bad ABC header surfaces as the std library's 'stoi' message with no file context.

Recommendation: Pick one convention for the unit (exceptions are simplest for an offline tool) and apply it in all four parsers; wrap stoi in a helper that names the field.


## F311 [UNVERIFIED] medium (reporter: medium) soundness — tools/durn_converter/parsers/musicxml_parser.h:578

**MusicXML durations are divided by the last <divisions> value seen, not the one in force when each note was parsed**

Reporters: tools-durn

Evidence: `parse() overwrites a single field per measure: musicxml_parser.h:481 `result.divisions = std::stoi(div_str);` while notes store raw counts (:400 `note.duration = std::stoi(dur_str);`). convert_to_figure_units then applies the final value to every note: :604-605 `fu.duration = static_cast<float>(note.duration) / static_cast<float>(result.divisions);`.`

Why: MusicXML allows <divisions> to change at any <attributes>; any such file gets every earlier note scaled wrong. Latent today because the function is dead, but it must be fixed before the MusicXML path can be wired up.

Recommendation: Store beats (duration / divisions-in-force) on MxmlNote at parse time, or carry a per-note divisions value.


## F312 [CONFIRMED-BY-MATT'S-CLAUDE] high (reporter: high) build — tools/engine_tests/CMakeLists.txt:1

**Neither test executable is registered with CTest; both silently depend on CWD = repo root; engine_tests has no warning flags**

Reporters: arch-build-headers, tools-misc-build, tools-tests

Evidence: `add_executable(engine_tests main.cpp)
target_link_libraries(engine_tests PRIVATE mforce_engine)`

Why: A grep for enable_testing/add_test/ctest across every CMakeLists.txt returns nothing, so 'ctest' runs zero tests and nothing in the build can gate on them; they are run by hand (STATUS.md cites counts). engine_tests reads 'patches/baselines/BaselineSIN.json' (844), 'scores/baselines/template_mary_walk.json' (1872) and, via MelodyProfile::load_by_name, 'styles/nursery_v1.json' (melody_profile.h:115) relative to CWD, with only a comment ('Run engine_tests from the repo root', 779) as protection. test_figures/CMakeLists.txt sets '/W4 /permissive-' (line 5) to match mforce_engine (engine/CMakeLists.txt:23) but engine_tests sets nothing, so test code compiles at a lower warning level than the engine it tests.

Recommendation: enable_testing() in the root CMakeLists; add_test(NAME engine_tests COMMAND engine_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}) and the same for test_figures; give engine_tests the same /W4 /permissive- block test_figures has. Then 'ctest -C Release' is the pre-commit gate instead of a hand-run.

Also reported as: No CTest integration: engine_tests is a bare executable nothing runs, and the root never calls enable_testing() | No CTest integration: engine_tests is a bare executable, nothing calls enable_testing()/add_test()


## F313 [UNVERIFIED] medium (reporter: medium) duplication — tools/engine_tests/main.cpp:9

**Two divergent hand-rolled test harnesses with different failure semantics and output**

Reporters: tools-tests

Evidence: `#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fails; \
  std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)`

Why: engine_tests (9-13) counts checks, continues on failure and prints only file:line (never which test). test_figures (36-59) uses EXPECT_EQ/EXPECT_NEAR that 'return 1' from the test function, evaluates its arguments twice, and RUN_TEST prints a named PASS/FAIL per test. Same repo, two incompatible idioms: a contributor adding a test must first learn which binary's dialect applies, and neither harness has per-test exception capture or a filter. The better properties are split between them (named tests in test_figures, check counting and NEAR tolerance printing in engine_tests).

Recommendation: One tools/tests/test_harness.h shared by both executables: TEST(name) registration, CHECK (continue) + REQUIRE (bail), CHECK_NEAR with values printed, per-test try/catch, --filter, and a final named summary. If a third-party single header is acceptable under engine/third_party, doctest.h delivers all of this in one file; otherwise ~80 lines hand-rolled once.


## F314 [UNVERIFIED] medium (reporter: medium) god-class — tools/engine_tests/main.cpp:144

**engine_tests/main.cpp is a 1930-line append-only single TU: 20 mid-file #includes, one header included twice, headers used before they are included, arbitrary run order**

Reporters: tools-tests

Evidence: `#include "mforce/render/perform_source.h"   // line 144
...
#include "mforce/render/perform_source.h"   // line 758
...
std::vector<float> tail(4800);               // line 380, <vector> first included at 620`

Why: 39 commits of appending (22 -> 754 checks) produced a file where each feature's tests arrive with their own #include block mid-file (15, 88, 144, 201, 224, 264, 411, 456, 493, 619-621, 734, 758, 771-776, 1045, 1174-1176, 1313-1314, 1617-1621). <algorithm> is never included although std::max (826), std::min (1001) and std::find (1704) are used; <utility> is not included for std::pair (344); <vector> is used at 380 and included at 620 — all rely on transitive includes from engine headers, so any header-hygiene change in the engine breaks the test build. main() runs walk2, walk3, walk1 then the DSP tests (1897-1925), an order no one chose. There is no way to run one test or one subsystem, so a 2.3M-sample Pierce sweep and a 20000-seed vary_steps search run every time someone checks a CurveNode tweak.

Recommendation: Split into per-subsystem TUs (test_curve.cpp, test_envelope.cpp, test_delay_loop.cpp, test_shaper.cpp, test_pierce.cpp, test_perform.cpp, test_comp_walk.cpp, ...) each self-registering via a static TestCase{name, fn} list in a shared test_harness.h, with main() supporting --filter <substr>. Every TU includes what it uses; the duplicate perform_source.h include and the pre-include std::vector use disappear by construction.


## F315 [UNVERIFIED] low (reporter: low) modern-cpp — tools/engine_tests/main.cpp:796

**hold_patch_json builds JSON with snprintf into a fixed char[1400]; truncation would be silent and surface as a parse exception**

Reporters: tools-tests

Evidence: `char buf[1400];
    std::snprintf(buf, sizeof(buf), R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2, "glideMs": %.1f },`

Why: The literal is ~1000 bytes today; the next added node pushes it past 1400 and snprintf truncates silently, producing invalid JSON that nlohmann rejects with an unrelated-looking error in a test far from the edit. C++20 std::format (or nlohmann::json construction with j["instrument"]["glideMs"] = glideMs then dump()) has no length ceiling and keeps the patch as data.

Recommendation: Build the patch as an nlohmann::json object and set glideMs on it; or std::format with {:.1f}.


## F316 [UNVERIFIED] low (reporter: low) smell — tools/engine_tests/main.cpp:824

**Signal-measurement helpers (RMS, peak, zero-crossing f0) are re-written inline per test with inconsistent conventions**

Reporters: tools-tests

Evidence: `auto rms_of = [](const std::vector<float>& b, float fromSec, float toSec) {
        int a = int(fromSec * 48000), z = int(toSec * 48000);`

Why: rms_of (824-830, hard-codes 48000 although 'sr' is in scope) is re-written as rms (1101-1106, different clamping) and again inline (1121-1124); peak-finding loops at 887-888 and 954-955; zero-crossing counting at 970-975 next to the interpolated f0 estimator at 917-931. The loop-compensation test's normalized autocorrelation (385-403) is another reusable measurement. Each copy carries its own off-by-one and clamping decisions, so the same quantity measured in two tests can differ.

Recommendation: tools/tests/test_signal.h with rms(buf, sr, from, to), peak(buf), f0_zero_crossing(buf, sr, from, to), period_autocorr(buf, minLag, maxLag). These are the same primitives the CMA-ES/refmetrics side needs in Python; keeping one C++ copy makes the WAV-gate and in-process assertions agree.


## F317 [UNVERIFIED] high (reporter: high) rt-safety — tools/engine_tests/main.cpp:831

**The non-negotiable 'no heap allocation in hot render loops' rule is asserted by no test anywhere in the repo**

Reporters: tools-tests

Evidence: `auto render_all = [](InstrumentPatch& ip, int n) {
        std::vector<float> b(size_t(n), 0.0f);
        RenderContext ctx{ip.sampleRate};
        ip.instrument->render(ctx, b.data(), n);`

Why: A grep for 'operator new', '_CrtSetAllocHook', 'alloc_count', 'AllocGuard' across *.h/*.cpp/CMakeLists returns nothing. The rule is enforced only by review, and the live RtAudio callback path (arch_rtaudio_live_audio) would glitch on a single std::vector growth inside next(). render_all already renders every baseline patch through Instrument::render, the exact place to count allocations.

Recommendation: Add a replaceable global operator new/delete pair in engine_tests (or _CrtSetAllocHook under MSVC) with a thread-local counter; wrap the render() call in render_all with a scope guard that asserts the counter did not move after prepare()/the first block. Run it over every patches/baselines/*.json and patches/library/**. This turns the project's top rule into a one-line regression gate.


## F318 [UNVERIFIED] medium (reporter: medium) soundness — tools/engine_tests/main.cpp:847

**Harness failure modes are crashes, not reports: CHECK continues after a failed null-check and the pointer is dereferenced; main() catches nothing; load_scoreless parses a possibly-unopened file**

Reporters: tools-tests

Evidence: `CHECK(pa != nullptr);
        pa->play_note({60.0f, 0.8f, 1.0f}, 0.0f);
 ...
        auto* pb = dynamic_cast<PitchedInstrument*>(b.instrument.get());
        pb->play_note({60.0f, 0.8f, 1.0f, 0.0f, false}, 0.4f);`

Why: The CHECK macro (9-10) increments g_fails and keeps going by design, so a failed 'pa != nullptr' is immediately followed by a null dereference: the run dies with an access violation and the summary line never prints. The same pattern recurs at 883-884, 903-904, 909-910, 950-951, 966-967 and 1115-1116 (pb, pi, pg, pn, pi2 are never checked at all). main() (1896-1929) has no try/catch, so a throw from MelodyProfile::load_by_name (runs first, 1897) or nlohmann::json::parse on an unreadable file in load_scoreless (780-787, no is_open() check) aborts the process with no indication of which test was running. When the harness fails, it fails without diagnostics.

Recommendation: Add a REQUIRE(cond) that returns from the test function on failure (the test_figures EXPECT_* early-return already does this); run each test through a wrapper that catches std::exception and reports the test name; have load_scoreless check is_open() and throw with the path. Fold both into the shared harness header recommended below.


## F319 [UNVERIFIED] low (reporter: low) smell — tools/engine_tests/main.cpp:1268

**Test writes a scratch file into renders/scratch/, a directory CLAUDE.md reserves for manual material, and creates it in whatever CWD it runs from**

Reporters: tools-tests

Evidence: `const std::string psgPath = "renders/scratch/_pm_test.psg";
        std::filesystem::create_directories("renders/scratch");
        { std::ofstream f(psgPath); f << "Eq Dq Cq Dq | Cq Cq Ch"; }`

Why: CLAUDE.md: 'renders/scratch/ for manual material'. A test leaving (on a failed run, before line 1309's remove) a .psg in Matt's manual area, or creating a renders/scratch tree inside build/ when run from the wrong directory, is a small but real rules collision; the comment at 1268 ('CWD-independent temp dir') describes the opposite of what the code does.

Recommendation: std::filesystem::temp_directory_path() / "mforce_pm_test.psg", removed in a scope guard so it is cleaned up even when a CHECK throws.


## F320 [UNVERIFIED] low (reporter: low) testing — tools/engine_tests/main.cpp:1551

**Statistical assertions are pinned to the exact Randomizer draw order; any refactor that changes draw sequence flips them without a behavioral regression**

Reporters: tools-tests

Evidence: `CHECK(fineCount > 20 && fineCount < 160);
        // Dotted dominates fine (~80%).
        CHECK(dottedCount * 2 > fineCount);
 ...
        CHECK(smallCount > 0 && smallStartSum / smallCount > 1.6);`

Why: walk2 (1551-1573), walk1 'cPicks < 15' over 100 seeds (1451-1460), walk3 'down > up' over 400 seeds (1839-1850) and 'distinct.size() >= 3' over 10 seeds (1891-1892), and test_figures 'anyDiff' on a single seed (446-449) all assert on counts from a specific RNG stream. They are deterministic, but a change in how many draws elaborate() or select_anchors() consume (or a Randomizer change) moves the counts with no change in the distribution being tested, and the failure message is a bare file:line with no observed count. Seeded-but-brittle.

Recommendation: Print the observed statistic in the CHECK message (a CHECK_MSG variant), widen N where cheap so the margin is several sigma, and note in each block what distributional property is being asserted so a future failure can be judged as regression vs. stream shift.


## F321 [UNVERIFIED] high (reporter: high) testing — tools/engine_tests/main.cpp:1896

**Coverage: ~14 of ~85 engine source/instrument types are exercised; the synthesis core has no tests and no golden checksums**

Reporters: tools-tests

Evidence: `int main() {
    run_walk2_tests();
    run_walk3_tests();
    ... run_pierce_passivity_tests();`

Why: Grepping engine/include for ': ValueSource' / ': WaveSource' / ': Instrument' yields ~85 concrete types. engine_tests instantiates 14 (ConstantSource, RefSource, CurveNode, Envelope, PerformSource/PerformOut, CombinedSource, DelayLineSource, SVFSource, ShaperSource, WormholeSource, PierceFilterSource, NameGate, plus SineSource and WhiteNoiseSource via JSON). Untested: the additive family (Partials, CompositePartials, ExpandRuleNode, Formant/FixedSpectrum/BandSpectrum/FormantSequence, Full/Basic/AdditiveSource2) which is the project's first pillar; every oscillator except Sine (Saw, Triangle, Pulse, FM, Wavetable, HybridKS, RedNoise); physical models (KSString, Mesh2DSource, AllpassResonator, BowTableSource, HammerBank); all 14 EvolutionSources; all noise sources except WhiteNoise; BW*/Biquad/DelayFilter/Reverb/Limiter/Vibrato; Range/Multi/Multiplex/Var/Crossfade/Distorted/Phased/Repeating/Segment sources; DrumKit; SourceRegistry; wav_writer; Randomizer stream stability. The only 'regression' check (838-856) compares two renders in the same process and its own comment says 'The null gate over 79 patches is the real referee' — so this unit cannot catch a hot-path regression on its own.

Recommendation: Add (1) a SourceRegistry walk: instantiate every registered type, set defaults, prepare(), next() 4096 samples, assert finite and bounded — one loop covers all ~85 types for NaN/inf/blow-up regressions; (2) a committed golden table (patch path -> hash of the first N render samples at a fixed seed) for patches/baselines/*.json so engine_tests alone flags a byte change; (3) a Randomizer golden (first 16 draws for a fixed seed) since every seeded comp assertion silently depends on it.


## F322 [UNVERIFIED] low (reporter: low) build — tools/mforce_cli/explore.cpp:158

**Relies on transitive includes for <cctype> and <stdexcept>**

Reporters: tools-cli

Evidence: `std::all_of(t.begin(), t.end(), [](char c){ return std::isdigit((unsigned char)c); })  -- no #include <cctype>; std::runtime_error thrown at 83, 94, 100, 105 ... with no #include <stdexcept>`

Why: Both currently arrive via nlohmann/json.hpp; a future bump of that header or an include-what-you-use pass breaks the build with no code change. main.cpp has the same pattern for std::runtime_error (332) and the `json` alias (529, via music_json.h).

Recommendation: Add <cctype> and <stdexcept> to explore.cpp; add <stdexcept> and <nlohmann/json.hpp> to main.cpp.


## F323 [UNVERIFIED] medium (reporter: medium) elegance — tools/mforce_cli/explore.cpp:313

**run_explore's 110-line while(true) with three goto next_variant and a double parse of the base patch**

Reporters: tools-cli

Evidence: `goto next_variant; (lines 324, 386, 391) ... next_variant:
        // Advance odometer-style across all axes
-- and the base patch is read twice: 262-264 `std::ifstream bf(basePatchPath); json basePatchPeek; if (bf) bf >> basePatchPeek;` then 282-284 `std::ifstream f(basePatchPath); ... f >> basePatch;``

Why: The gotos exist only because the variant body is inlined into the odometer loop; extracting it makes the three failure exits plain `continue`s and the success path a value return. The 2nd parse exists only to check `contains("instrument")` which the already-loaded `basePatch` answers. The comment at 334-336 ("load_patch_file currently takes a path, not an in-memory JSON tree") is half stale: `load_instrument_patch_json` exists (patch_loader.h:33) for the instrument branch; the on-disk write is still needed because the variant patch is a manifest deliverable, so only the comment needs correcting.

Recommendation: Extract `std::optional<json> render_variant(const json& variantPatch, const std::filesystem::path& outDir, const std::string& id, const ExploreCfg&)` returning the manifest entry; the loop becomes build → render_variant → push/record-failure → advance. Peek `basePatch.contains("instrument")` after the single load. Use load_instrument_patch_json for the instrument branch and fix the comment.


## F324 [UNVERIFIED] low (reporter: low) smell — tools/mforce_cli/explore.cpp:324

**goto-based loop control in explore and unchecked atof/duplicated WAV writer across the stk_ref harnesses**

Reporters: arch-modern-cpp

Evidence: `explore.cpp:324 `goto next_variant;`, 386, 391; stk_ref: `if (argc > 2) kSampleRate = atof(argv[2]);` in brass_ref.cpp:68, flute_ref.cpp:68, clarinet_ref.cpp:71, blowhole_ref.cpp:75, saxofony_ref.cpp:77, bandedwg_ref.cpp:73, bowed_ref.cpp:85, mesh2d_ref.cpp:263; `write_wav16_mono` copied verbatim in all eight (brass 24-46, flute 23-45, clarinet 26-48, blowhole 28-50, saxofony 28-50, bandedwg 28-50, bowed 31-55, mesh2d 192-213)`

Why: atof returns 0 on garbage, which then sets Stk sample rate 0 and divides by zero in frame counts; the eight-way copy of the WAV writer is harness-only cost but any header-format fix has to be made eight times. goto in explore is readable but a `continue` in a lambda/for would be idiomatic.

Recommendation: Share a tiny stk_ref_common.h (WAV writer, midi_hz, argv parsing with std::from_chars validation); replace goto with a helper returning bool.


## F325 [CONFIRMED-BY-MATT'S-CLAUDE] critical (reporter: critical) soundness — tools/mforce_cli/explore.cpp:361

**Explore instrument-mode pull loop violates the StreamingVoice contract: no performSource->tick(), no advanceList advance**

Reporters: tools-cli

Evidence: `for (int i = 0; i < frames; ++i)
    mono[size_t(i)] = soft_clip(sv.source->next() * sv.gain);
-- vs instrument.h:254-263: "Streaming callers MUST call performSource->tick() once per sample before source->next() ... MUST tick each [advanceList] entry once per sample AFTER source->next(), or tap-closed feedback loops fall silent"`

Why: The comment at 358-360 claims this "matches mforce_ui's live audio path", but the UI loop (tools/mforce_ui/main.cpp:13286-13290) and the engine's own render (instrument.h:444-446) both do tick → next → advance. prepare_voice_at returns vg.advanceList (instrument.h:371-372) precisely so callers advance tap-only loop tails. Sweeping any tap-closed feedback-loop patch (flute_default/oboe_default family) in --explore instrument mode therefore renders silence or a wrong signal, and any bend/wheel binding freezes at note-on. The manifest stats then rank garbage. Verified by reading the contract, the engine render loop, and the UI loop; not executed.

Recommendation: Add one engine helper, e.g. `void pull_streaming_voice(const PitchedInstrument::StreamingVoice& sv, float* out, int n)` in instrument.h that does tick/next/advance, and use it from both explore.cpp and tools/mforce_ui/main.cpp:13286-13290 so the contract lives in one place. Until then, replicate the three-step loop here.


## F326 [UNVERIFIED] low (reporter: low) soundness — tools/mforce_cli/explore.cpp:507

**--explore-filter parses the next argv as float before validating the flag, so unknown flags die with 'ERROR: stof'**

Reporters: tools-cli

Evidence: `if (i + 1 < argc && parse_stat_flag(a, std::stof(argv[i + 1]))) { ++i; continue; }
std::cerr << "Unknown flag: " << a << "\n";`

Why: Function arguments are evaluated before the call, so `--explore-filter m.json --bogus --json` throws std::invalid_argument from stof and reaches main's catch as "ERROR: stof" instead of the intended "Unknown flag: --bogus" (and `--peak-min abc` gives the same unhelpful message). The usage text and the Unknown-flag branch are therefore only reachable when the bad flag is last.

Recommendation: Split parse_stat_flag into name validation (returns the StatBound* or nullptr) and then parse the value with std::from_chars / a try-catch that names the flag.


## F327 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_cli/main.cpp:103

**CLI render epilogue (mono->stereo, peak/rms, 'Wrote:' report) is copy-pasted seven times; UI chord render duplicates CLI run_chords**

Reporters: arch-duplication, arch-modern-cpp, arch-ui-tools

Evidence: `tools/mforce_cli/main.cpp `std::vector<float> stereo(frames * 2); for (int i = 0; i < frames; ++i) { stereo[i * 2] = mono[i]; stereo[i * 2 + 1] = mono[i]; } ... float peak = 0.0f; double rms = 0.0;` at 103-133, 185-206, 324-347, 616-639, 720-742, 886-900, 1004-1008 (explore.cpp 364-368 an eighth interleave). tools/mforce_ui/main.cpp render_chords_waveforms 8098-8177 repeats run_chords 79-101: Part from ParsedChord, `conductor.chordPerformer.defaultSpreadMs = ...; conductor.perform(part, bpm, *ip.instrument); float totalSeconds = part.totalBeats() * 60.0f / bpm + 1.0f;` and a peak-normalize that run_compose also carries (606-614).`

Why: Seven copies of the same 25 lines diverge (some print nonzero, some add 1 s tail, some 2 s, drums use 0.95/1.0 thresholds vs chords 0.95/0.95); a change to the stereo writer or stats has to be made in eight places.

Recommendation: Engine helpers in mforce/render: render_part_mono(part, bpm, instrument, tailSec) and write_stereo_wav_report(path, mono, sr) returning stats; CLI modes and the UI chord/drum paths call them.

Also reported as: Mono-to-stereo interleave + write_wav_16le_stereo block duplicated seven times | Mono-to-stereo + WAV write + peak/RMS block copied ~15 times across CLI, UI, tests


## F328 [UNVERIFIED] high (reporter: high) duplication — tools/mforce_cli/main.cpp:116

**beats→frames→render→stereo→peak/rms pipeline hand-rolled 6-7 times in main.cpp, again in explore.cpp and the UI, while engine signal_stats.h already provides the stats**

Reporters: tools-cli

Evidence: `float peak = 0.0f; double rms = 0.0; ... for (auto s : stereo) { float a = std::fabs(s); if (a > peak) peak = a; rms += double(s) * double(s); } rms = std::sqrt(rms / stereo.size());
-- identical at main.cpp 194-201, 335-342, 628-635, 731-738, 791-800, mforce_ui/main.cpp:13311-13313; engine/include/mforce/util/signal_stats.h:21-40 compute_time_stats does exactly this and explore.cpp:369 already calls compute_all_stats`

Why: Three blocks are copied verbatim: (a) peak/rms (6× main.cpp + UI), (b) mono→stereo interleave `stereo[i*2] = mono[i]; stereo[i*2+1] = mono[i];` at main.cpp 105-108, 186-189, 326-329, 617-620, 721-724, 887-890, 1005-1008, explore.cpp:365-368, mforce_ui/main.cpp:8528-8532, 13081-13083, 13303-13307, (c) `totalSeconds = totalBeats*60/bpm + 2; frames; mono; RenderContext; render` at main.cpp 178-181, 310-319, 591-600, 715-718, 877-880, 987-990 and mforce_ui/main.cpp:8129-8133. Each copy has drifted (+1s vs +2s tail, nonzero count in two of them, preroll in one, normalize in one), so a fix to one (e.g. the tempo bug below) must be applied in six places and will be missed.

Recommendation: One helper in the engine (render/ or util/): `SignalStats render_mix_to_wav(std::span<Instrument*> insts, int sampleRate, float seconds, const std::string& path, float prerollSec = 0)` that renders/sums, interleaves and calls compute_time_stats; plus `write_wav_16le_mono_as_stereo(path, sr, mono)` so no caller interleaves by hand. Then run_chords/render_piece_to_wav/render_and_write/run_compose/run_play/run_dun/test_ornaments and the three UI sites collapse to one call each.


## F329 [UNVERIFIED] low (reporter: low) smell — tools/mforce_cli/main.cpp:141

**build_descending_phrase: unused stepDown parameter, misleading name, redundant first-iteration unroll**

Reporters: tools-cli

Evidence: `static Phrase build_descending_phrase(Pitch startPitch, const MelodicFigure& repFig, int reps, int /*stepDown*/, const MelodicFigure& tailFig) { ... phrase.add_melodic_figure(repFig); for (int i = 1; i < reps; ++i) { phrase.add_melodic_figure(repFig); }`

Why: Nothing descends and the step argument is ignored at all three call sites (262-264 pass -2). The unrolled first add plus a loop from 1 is just `for (i < reps)`. It only serves the orphaned --melody mode (see the dead-modes finding) and misleads a reader into thinking the phrase steps down.

Recommendation: Delete with --melody; if kept, rename to build_repeated_phrase(start, fig, reps, tail) and drop the dead parameter.


## F330 [UNVERIFIED] low (reporter: low) soundness — tools/mforce_cli/main.cpp:592

**run_compose mixes all instruments at the alphabetically-first patch's sampleRate with no equality check**

Reporters: tools-cli

Evidence: `int sampleRate = patchByPath.begin()->second.sampleRate; ... for (auto* inst : instruments) { ... RenderContext _ctx{sampleRate}; inst->render(_ctx, buf.data(), frames); ... }`

Why: patchByPath is a std::map keyed by path, so begin() is whichever path sorts first, not the CLI patch. Instrument::render ignores ctx (instrument.h:62 `render(const RenderContext& /*ctx*/, ...)`) and voices were pre-rendered at each instrument's own rate, so a melody patch at 44100 summed with an accompaniment at 48000 would play at the wrong speed/pitch silently and be written with the wrong header. Both default comp patches are 48000 today (oboe1.json:446, piano_default.json:1084), so no observable bug yet.

Recommendation: After loading, assert every InstrumentPatch.sampleRate equals the first and fail with a clear message otherwise; take the rate from the CLI patch, not map order.


## F331 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_cli/main.cpp:709

**Output buffer length uses a single section's bpm while Conductor::perform honours per-section tempo and truncation**

Reporters: tools-cli

Evidence: `float bpm = 120.0f; for (auto& s : piece.sections) { totalBeats += s.beats; bpm = s.tempo; } float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
-- conductor.h:548-553: `for (const auto& section : piece.sections) { float bpm = section.tempo; ... float effectiveBeats = section.beats - section.truncateTailBeats;``

Why: run_play (709-716) and render_piece_to_wav (173-178) use the LAST section's tempo; run_compose (590) and run_dun (876) use the FIRST. music_json.h:551/558 round-trips per-section `tempo`, so a --play piece with sections at e.g. 60 then 180 bpm is scheduled correctly by the Conductor but rendered into a buffer sized for the wrong tempo — truncating the tail or padding seconds of silence. truncateTailBeats is subtracted only in run_dun (874) although the Conductor subtracts it for every piece (553). Composed/DUN pieces are single-tempo today, so this is latent, but it is reachable via --play and will bite the moment the comp lane adds a tempo change.

Recommendation: Add `float Piece::total_seconds(float tailSec) const` (or a free function next to Conductor) = Σ max(0, beats − truncateTailBeats) × 60 / section.tempo, and use it from every CLI mode and the UI chord path. Remove the four ad-hoc loops.


## F332 [UNVERIFIED] medium (reporter: medium) smell — tools/mforce_cli/main.cpp:968

**--test-ornaments, --josie and --melody are unrunnable or orphaned dev harnesses carrying hard-coded paths and song data**

Reporters: tools-cli

Evidence: `auto ip = load_instrument_patch("patches/PluckU.json"); if (!ip.instrument) { std::cerr << "ERROR: could not load PluckU.json\n"; return 1; }
-- PluckU.json exists only at patches/old/PluckU.json; load_instrument_patch throws on a missing file (engine/src/patch_loader.cpp:48-50 `throw std::runtime_error("Cannot open patch file: "...)`), so the null check is dead.`

Why: `--test-ornaments` cannot run from the repo root (file moved to old/), and its only guard is unreachable. `--josie` (398-401) requires `<dir>/guitar_pluck.json`, `fm_bass.json`, `kick_drum.json`, `snare_drum.json`, none of which exist under patches/ (only in stale .claude/worktrees), and embeds a 12-line chord chart for one song (363-374) plus per-instrument mix levels. `--melody` (213-302) builds a fixed three-figure phrase and is referenced nowhere outside main.cpp. These three modes are ~300 lines (24% of the file) that cannot be exercised, yet sit beside the live modes and are dispatched from main() 1221-1227 as if supported.

Recommendation: Move --test-ornaments into tools/engine_tests with a fixture patch under patches/baselines/ (per the regression-scope rule); delete --josie and --melody (git history keeps them) or, if the josie chord chart is still wanted, make it a score file under scores/ and a generic --score mode. Then run_chords is the only hand-built-Part mode left.


## F333 [UNVERIFIED] low (reporter: low) smell — tools/mforce_cli/main.cpp:968

**Stale/dead items: --test-ornaments loads a patch that does not exist; write-only nodeMap; local with static-prefix name; per-sample cos/sin for constant pan**

Reporters: arch-ui-tools

Evidence: `mforce_cli/main.cpp 968 `auto ip = load_instrument_patch("patches/PluckU.json");` — no such file (patches/PluckU.json absent), so --test-ornaments always fails at 969-971. mforce_ui/main.cpp 1613 `std::unordered_map<std::string, GraphNode*> nodeMap;` written at 1751, never read. 13558 `bool s_dockLayoutInitialized = false;` is a local inside main() using the file-static prefix. audio_callback 3966-3967 `float aL = std::cos(t * 0.5f * 3.14159265358979323846f); float aR = std::sin(...)` recomputed per sample per channel even when pan is a pin constant.`

Why: A permanently failing test mode gives false confidence that it is covered; the dead map is a dangling-pointer trap for the next reader; the naming nit misleads about scope; the trig is wasted RT work.

Recommendation: Point test_ornaments at a tracked library patch or delete the mode; delete nodeMap; rename the local; cache pan gains and recompute only when the pan source is non-constant.


## F334 [UNVERIFIED] low (reporter: low) smell — tools/mforce_cli/main.cpp:1217

**Eleven copy-pasted dispatch ifs; unknown --flags fall through to run_patch and report 'Patch file not found: --flag'**

Reporters: tools-cli

Evidence: `if (argc >= 2 && std::string(argv[1]) == "--dump-descriptors") return run_dump_descriptors(argc, argv); ... (11 lines) ... return run_patch(argc, argv);
-- run_patch 759-761: `if (!std::filesystem::exists(patchPath)) { std::cerr << "Patch file not found: " << patchPath``

Why: Each new mode adds another near-identical line and there is no central usage listing; a typo such as `--explor` is reported as a missing patch file rather than an unknown mode, which has already cost debugging time in agent runs that drive this binary.

Recommendation: A constexpr/static table `{const char* flag; int (*fn)(int, char**); const char* usage;}` looped once; if argv[1] starts with "--" and matches nothing, print the table and return 1.


## F335 [UNVERIFIED] medium (reporter: medium) build — tools/mforce_ui/CMakeLists.txt:4

**mforce_ui is a single 14,658-line translation unit; every UI edit recompiles the whole UI plus all engine headers**

Reporters: tools-misc-build

Evidence: `add_executable(mforce_ui
  main.cpp  -- tools/mforce_ui/main.cpp is 14,658 lines (wc -l) and produces a 15,197,812-byte main.obj; it includes 59 headers, 25 of them engine headers.`

Why: This is the build-side cost of the UI god-file (the file itself belongs to the UI reviewer): there is no incremental compilation at all for the UI, and PCH alone cannot help because the UI code, not just the headers, is the bulk of the TU.

Recommendation: Split main.cpp into per-panel TUs (graph editor, transport, keyboard, audio thread, settings) listed here; pair with the PCH recommendation so the shared engine headers are parsed once.


## F336 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/CMakeLists.txt:9

**imgui_demo.cpp is compiled into mforce_ui but ShowDemoWindow is never called**

Reporters: tools-misc-build

Evidence: `${IMGUI_DIR}/imgui_demo.cpp  listed in add_executable; grep -c ShowDemoWindow tools/mforce_ui/main.cpp = 0; the resulting imgui_demo.obj is 1,372,355 bytes.`

Why: Dead code compiled on every clean build (the linker's /OPT:REF drops it from the exe, so the cost is build time only).

Recommendation: Remove the line, or guard it behind an option(MFORCE_UI_DEMO OFF) for the rare time the widget gallery is wanted.


## F337 [UNVERIFIED] medium (reporter: medium) build — tools/mforce_ui/CMakeLists.txt:30

**Windows-only surface is concentrated in mforce_ui but is unguarded at both the CMake and source level**

Reporters: arch-build-headers

Evidence: `tools/mforce_ui/CMakeLists.txt:30-37 `target_link_libraries(mforce_ui PRIVATE mforce_engine glfw rtaudio rtmidi opengl32 comdlg32)` (outside the `if (MSVC)` that starts at :39); tools/mforce_ui/main.cpp:11 `#include <windows.h>` unconditional, :1284 and :1322 `if (GetOpenFileNameA(&ofn))`, :12950 `SetUnhandledExceptionFilter(seh_crash_filter);`; tools/mforce_ui/build_stamp.h:23 `#include <windows.h>`, :202 `GetModuleFileNameA(nullptr, exePath, MAX_PATH)`; tools/mforce_cli/explore.cpp:218 `#ifdef _WIN32` (guarded). Engine side: only core/denormals.h:8,15 `#if defined(_MSC_VER) || defined(__SSE2__)`; 30 occurrences of literal `3.14159...` across engine headers (e.g. wave_evolution.h:530, ks_string.h:462, filters.h:63) instead of C++20 `std::numbers::pi`.`

Why: CLAUDE.md accepts Windows-only for now but asks to plan for cross-platform; today the engine is clean, so the whole porting cost sits in mforce_ui's 14,658-line main.cpp where Win32 file dialogs, SEH and WinMain plumbing are interleaved with ImGui code rather than isolated. Linking opengl32/comdlg32 unconditionally breaks configure on any other platform before a single source compiles.

Recommendation: Move file-dialog, crash-filter and exe-path code into tools/mforce_ui/platform_win.cpp behind a 3-function `platform.h`; wrap the Windows link libs and WIN32_EXECUTABLE/ENTRY in `if (WIN32)`; in the engine, replace PI literals with `std::numbers::pi_v<float>` (<numbers> is header-light).


## F338 [UNVERIFIED] medium (reporter: medium) build — tools/mforce_ui/CMakeLists.txt:40

**Warning policy is copy-pasted per target, inconsistent, and never enforced (no /WX anywhere)**

Reporters: tools-misc-build

Evidence: `mforce_ui: 'target_compile_options(mforce_ui PRIVATE /W3)'; stamp_test/CMakeLists.txt:8 '/W3'; engine_tests/CMakeLists.txt:1-2 sets no options at all; engine/CMakeLists.txt:23, mforce_cli:5, test_figures:5, ppl_to_json:10, durn_converter:7 each repeat 'if (MSVC) target_compile_options(... /W4 /permissive-)'. grep '/WX' across all CMakeLists: no hits.`

Why: The 14,658-line UI TU and the test harness are the least-warned code in the repo, and the five identical blocks will drift further. Without /WX, /W4 on the engine is advisory: nothing fails when a new warning appears, so the count can only grow. Five copies of the same block is exactly the build-logic duplication the brief asks about.

Recommendation: Define once in the root: add_library(mforce_warnings INTERFACE); target_compile_options(mforce_warnings INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /WX /external:anglebrackets /external:W0>) and target_link_libraries every first-party target against it (third-party subdirs keep their own flags). Bring mforce_ui and engine_tests to /W4 and fix what surfaces; then /WX holds the line.


## F339 [CONFIRMED-BY-MATT'S-CLAUDE] high (reporter: high) god-class — tools/mforce_ui/main.cpp:1

**mforce_ui/main.cpp is a 14,658-line single translation unit with 349 file-scope statics; every UI hack in this census lives in it**

Reporters: arch-hacks-census

Evidence: ``wc -l` = 14658; `grep -c '^static '` = 349 (e.g. :737 `static nlohmann::json s_loadedParamMap`, :784 `static bool s_graphDirty`, :933 `static std::mutex g_audioMutex`, :4611-4614 live cache globals, :4239 `static std::vector<StreamEnvHold> g_streamEnvHolds`); forward declarations of same-file functions are needed to order it (:4596 vs :4716 collect_envelopes, :9032 vs :12241 draw_formant_strip).`

Why: Loader conversion (paramMap), audio-thread state, MIDI, file dialogs, strip rendering, group projection, clipboard and the legacy mapping dialog share one namespace of mutable globals; the backlog's own perf item 76 and the three editor/loader asymmetries (35) were all found by reading this file. Any public release would have to split it before a second contributor could work on it.

Recommendation: Split by subsystem (graph model + serialization, DSP wiring/live audio, node drawing, dialogs/menus, legacy paramMap) into separate TUs with a small shared state struct; this unit flags it as the structural home of the other UI findings — detailed decomposition belongs to the UI review unit.


## F340 [UNVERIFIED] medium (reporter: medium) smell — tools/mforce_ui/main.cpp:132

**No single source of truth for UI-special node types: five hand-maintained lists that have already drifted once**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `132-135 `is_special_ui_type` lists SoundChannel/StereoMixer/PatchOutput/Parameter only; 1721-1731 unknown-type test must therefore spell `!is_special_ui_type(type) && type != NT_ENVELOPE && type != NT_PERFORM && ... type != "FormantSpectrum" && !SourceRegistry::instance().has(type)`, with comment 1724-1728: 'The exemption list predates PerformNode and fired a false "loaded inert" on every converted patch's first human load'. The same set is re-enumerated in node_display_name 137-143, node_title_color 168-180, create_dsp 386-456, build_pins 536-595. 166 `kDynPinGold = IM_COL32(205, 170, 60, 255)` is re-spelled literally at 178 `return IM_COL32(205, 170, 60, 255);` despite the comment that it must 'read as one color'.`

Why: The recorded bug is the predictable cost: each new editor-only type (Wormhole, NameGate, PerformNode were all added this way) must be added to every list by hand, and a miss produces a silent misclassification (inert node, wrong color, wrong pins).

Recommendation: One table `struct UiTypeInfo { const char* name; const char* display; ImU32 color; bool hasEngineObject; ... }` keyed by type name, with `is_known_type(t) = ui_table.contains(t) || registry.has(t)`; derive display name, color and the unknown-type check from it; use kDynPinGold at 178.


## F341 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:152

**UI classifies node types by substring match on the type name instead of the registry's SourceCategory**

Reporters: arch-hacks-census

Evidence: `tools/mforce_ui/main.cpp:152-155 `static bool is_noise_type(const std::string& t) {
    return t.find("Noise") != std::string::npos
        || t == "SegmentSource";` :158-161 `t.find("Evolution")`; :4724 `if (std::string_view(vs->type_name()).find("Multiplex") != std::string_view::npos) return false;``

Why: The registry already carries a category per type (used two lines later at :191). A future 'NoiseGate' filter or 'EvolutionMixer' would be mis-coloured, and the Multiplex substring check decides whether live envelopes are gated — a behavioural decision keyed on a name fragment. This is the reflection-like string sniffing the project rules say to avoid in favour of explicit registries.

Recommendation: Add the two display sub-categories (Noise, Wavetable) to SourceCategory or a registry flag, and give MultiplexSource an explicit `is_container()` virtual (or registry flag) for the envelope walk.


## F342 [UNVERIFIED] medium (reporter: medium) god-class — tools/mforce_ui/main.cpp:245

**GraphNode is a tagged-union-by-convention god struct: every node carries every node type's fields, dispatch is typeName string if-chains, and the constructor reads global s_nodes during its own emplace_back**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `PatchOutput-only: 267 `int polyphony{4};`, 274-275 `bool sustaining{false}; char onsetsBuf[128]{};`, 279-280 glideMs; Parameter/NameGate-only: 311-312 `std::string paramName; char paramNameBuf[32]{};`; Perform-only: 324 `int performField{0};` (never referenced anywhere else in the file), 332 perfFieldIds; Curve-only: 336-341; FormantSpectrum-only: 264; display: 299 `std::vector<float> waveformData;`. Dispatch: create_dsp 386-466 and build_pins 536-622 are `if (typeName == NT_...)` chains. 369 `GraphNode(const std::string& type) : id(next_id()), typeName(type), label(unique_node_label(type))` where 707-718 unique_node_label iterates `s_nodes` — the container the node is being emplaced into (1733 `s_nodes.emplace_back(type);`), requiring the forward declaration at 243.`

Why: Adding a node kind means touching five hand-maintained lists (see separate finding) and growing every node; type-specific invariants (e.g. perfFieldIds only meaningful on Note faces) are enforced nowhere. The ctor's dependence on the global container makes GraphNode unconstructible outside the editor (tests, headless tools) and relies on the container's unspecified mid-insertion state. The load path immediately overwrites the computed label (1735 `gn.label = id;`), so the O(N) uniqueness scan per node is wasted on every load.

Recommendation: Keep the shared core (id, typeName, label, pins, dspSource, settings/arrays, gridPos) and move per-kind payload into a `std::variant<Monostate, PatchOutputData, ParameterData, NoteFaceData, CurveData, FormantTableData>` or small per-kind structs; take the label as a ctor argument (caller computes uniqueness) so the ctor is pure; delete performField; replace the fixed char buffers with std::string + ImGui string callbacks.


## F343 [UNVERIFIED] low (reporter: low) efficiency — tools/mforce_ui/main.cpp:611

**Instantiate-to-introspect: every node creation constructs its DSP object twice, and the paramMap converter constructs one per entry, because descriptors are only reachable through an instance**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `611 `auto tmp = reg.create(typeName, DSP_SAMPLE_RATE);` (build_pins, discarded after reading descriptors), then 462 `dspSource = reg.create(typeName, DSP_SAMPLE_RATE);` (create_dsp); 565 `Envelope tmp(DSP_SAMPLE_RATE);` for the same purpose; 1488-1495 `is_setting` does `auto probe = reg.create(type, DSP_SAMPLE_RATE);` per paramMap entry. source_registry.h (28-59) exposes only `create`, `has`, `get_category`, `registered_types` — no descriptor-only path.`

Why: Load-time only, so not a release blocker, but it doubles construction cost for sources whose constructors allocate sample-rate-sized state and makes 'what pins does type X have' impossible to answer without building X. The fix is cross-unit (registry), which is why it keeps being worked around here.

Recommendation: Register descriptor spans alongside the factory (`register_type(name, category, factory, descriptors, configurator)`) and add `SourceRegistry::descriptors(type)`; build_pins and is_setting then need no instance.


## F344 [UNVERIFIED] low (reporter: low) elegance — tools/mforce_ui/main.cpp:636

**Link direction is an enforced invariant (start = output, end = input) that is undocumented and re-checked in both orientations at three sites**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `636-641 `struct Link { int id; int startPinId; int endPinId; ...}` with no comment. Every creation site stores (output, input): 2062/2067/2081/2090/2100/2113/2148 (`s_links.emplace_back(outIt->second, inIt->second)`), editor 14303-14304 `int outPin = (startPin->kind == PinKind::Output) ? startAttr : endAttr; int inPin = ...` then 14342 `s_links.emplace_back(outPin, inPin);`, 9189-9191, 10521, 10608-10609, 10671. Yet 950-952 `if (link.endPinId == pin.id) otherPinId = link.startPinId; if (link.startPinId == pin.id) otherPinId = link.endPinId;`, 1007-1008, and 1042-1046 (`else if (endPin->kind == PinKind::Output && startPin->kind == PinKind::Input)`) each handle a reversed link that cannot exist.`

Why: Dead branches cost reading time and imply a flexibility that is not there; a future reader may 'fix' one site to accept reversed links and break the others.

Recommendation: Rename the fields `outPinId`/`inPinId`, assert the kinds in the Link ctor, and delete the reversed-orientation branches at 952, 1008 and 1044-1046.


## F345 [UNVERIFIED] medium (reporter: medium) efficiency — tools/mforce_ui/main.cpp:649

**std::vector<GraphNode> with a non-noexcept move: every reallocation deep-copies all nodes, and GraphNode* handles are invalidated by any emplace_back**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `649 `static std::vector<GraphNode> s_nodes;` GraphNode holds 332 `std::map<std::string, std::string> perfFieldIds;` and 354-355 two `std::unordered_map<std::string, int>`; MSVC xtree 902-907: `_Tree(_Tree&& _Right) ... { _Alloc_sentinel_and_proxy(); ... }` (no noexcept, allocates), so GraphNode's implicit move is not noexcept and vector growth uses the copy ctor: three nlohmann::json members (296, 348 plus pins' shared_ptrs), 299 `std::vector<float> waveformData;`, strings and maps are copied per node per reallocation. Pointers are handed out freely: 814-819 find_node_for_pin, 1751 `nodeMap[id] = &gn;` (dead, but exactly the pattern that dangles), 2307-2309 `std::vector<GraphNode*> strip`.`

Why: Loading or pasting N nodes copies ~2N full nodes including their waveform strips; worse, correctness of every `GraphNode*` in the file depends on no emplace_back happening while it is held — a hazard with no compiler help, in a 14k-line TU that stores such pointers in lambdas and locals.

Recommendation: Use a stable-address container (`std::deque<GraphNode>` or `std::vector<std::unique_ptr<GraphNode>>`) so node pointers are stable for the node's lifetime and growth never copies; or at minimum give GraphNode an explicit `noexcept` move ctor/assign (MSVC map move will still allocate — the stable container is the real fix).


## F346 [UNVERIFIED] medium (reporter: medium) workaround-hack — tools/mforce_ui/main.cpp:722

**s_loadedParamMap is documented as dead residue but is still a live, edited binding model; two contradictory comments in this range**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `722-725: 'paramMap RESIDUE — the entries convert_parammap_to_wiring could NOT turn into graph nodes. No longer the model (P2b, 2026-08-19) ... the curve editors and Mappings dialog derive from the graph, not from here.' 734: 'this is expected to be EMPTY for most patches'. Versus 2150-2155: 'paramMap: the stash IS the model (2026-08-14 spec §2) ... bindings live as data, are edited in the Mappings dialog / Curve Properties' followed by `s_loadedParamMap = root["instrument"]["paramMap"];`. Writers elsewhere: 7110-7119 `s_loadedParamMap[paramName] = json::array(); ... e.push_back(std::move(curveObj));` (curves_add_entry), 10708-10709 `s_loadedParamMap["frequency"] = n.label + ".frequency";`.`

Why: Two binding mechanisms (paramMap stash applied per note vs. PerformNode/CurveNode wiring + dynamicPins) coexist, and the declaration lies about which is canonical. Anyone reading 722-737 would conclude the Mappings-dialog writes are dead code and remove them, or conversely keep extending a model the design says is retired. This is the kind of half-migration the review brief calls a workaround.

Recommendation: Decide: either finish P2b (convert owned-formant targets by letting FormantRow carry a driven ref, then delete the stash and the Mappings-dialog writers) or retitle the stash as a first-class model and delete the 'residue' narrative. Until then, make 722-737 state the truth: 'still written by curves_add_entry / convert paths; applied per note by apply_param_map'.


## F347 [UNVERIFIED] medium (reporter: medium) efficiency — tools/mforce_ui/main.cpp:806

**Linear-scan pin/node lookups nested inside per-frame and per-edit loops; vector<GraphNode> hands out raw pointers that emplace_back invalidates**

Reporters: arch-ui-tools

Evidence: `find_pin (806-812) and find_node_for_pin (814-820) scan all nodes x pins. Nested uses: update_node_dsp_unlocked `for (auto& pin : node.inputs) { for (auto& link : s_links) { ... find_node_for_pin(otherPinId); ... find_pin(otherPinId);` (948-956) called for every node by update_all_dsp (1026); per link per frame in link drawing 14006-14016 plus two more find_node_for_pin at 14015-14016; per dynamic pin per frame `for (auto& c : s_nodes) if (c.label == ref)` (14049); wormhole_twin (831-840) per selected wormhole per frame (14101-14104). `static std::vector<GraphNode> s_nodes;` (649) with GraphNode a ~30-member struct (245-634: two nlohmann::json, two unordered_maps, a std::map, 128+32-byte buffers) forces 'emplace_back may reallocate s_nodes — re-find both after' rituals (11164-11173, 11239-11240, 10580, 10687) and left a write-only pointer map `nodeMap[id] = &gn;` (1613, 1751, never read).`

Why: update_all_dsp is roughly O(N·P·L·N·P) and runs on every link change under the audio lock (compounding the RT finding); frame cost grows quadratically with patch size; and pointer invalidation is a standing UAF hazard that is currently avoided only by discipline.

Recommendation: Stable node storage (vector<unique_ptr<GraphNode>> or slot map) plus pin->node and label->node indexes rebuilt on structural change; make find_pin/find_node_for_pin O(1).


## F348 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:910

**Dead code in this range: dsp_rewire_link, GraphNode::performField, load's nodeMap, and the waveOut-era <mmsystem.h> include**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `910 `static void dsp_rewire_link(int outputPinId, int inputPinId, bool connect) {` — no call site in the file (grep: definition only). 324 `int performField{0};` — only occurrence in the file. 1613/1751 nodeMap written, never read. 12 `#include <mmsystem.h>` — the only other mentions of waveOut are comments (3542, 14565); the RtAudio design replaced it.`

Why: Each is a small trap: dsp_rewire_link looks like the per-link rewire entry point but is not (update_all_dsp is), performField is documented as the field selector but is ignored, and the stale include drags in the multimedia API for nothing.

Recommendation: Delete all four.


## F349 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:938

**Editor wires every unconnected input-descriptor pin to a 0.0 stand-in constant, clobbering engine defaults (backlog 35c), and reconstructs wires by exact pin name so loader aliases drop silently (35a)**

Reporters: arch-hacks-census

Evidence: `tools/mforce_ui/main.cpp:613 `inputs.emplace_back(desc.name, PinKind::Input, 0.0f, true, desc.multi, desc.hint);` then :938-944 `// First pass: wire all pins to their ConstantSource defaults ... node.wire_pin(pin.name, pin.constantSrc);`; the loader-only 'cutoff' alias at engine/src/patch_loader.cpp:849-851 has no editor counterpart.`

Why: dsp/BACKLOG.md:108-121 lists three ways a CLI-legal patch mangles in the editor; this is the mechanism. WavetableSource's WhiteNoise default input never applies in the editor because 'inputSource' is force-wired to ConstantSource(0.0), and a patch using 'cutoff' loses its wire on load. The UI and CLI therefore render the same file differently — the one property a patch editor must not have.

Recommendation: Leave input-only pins unwired when no link exists (let the engine default stand), share one alias table between loader and editor, and gate with the existing roundtrip harness (backlog 79 notes the harness already finds render diffs).


## F350 [UNVERIFIED] high (reporter: high) rt-safety — tools/mforce_ui/main.cpp:1022

**update_all_dsp / update_node_dsp do allocating, graph-size-proportional work while holding the mutex the RtAudio callback blocks on**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `1023: `std::lock_guard<std::mutex> lock(g_audioMutex);` then 1032: `std::unordered_map<int, std::vector<Consumer>> consumers;`, 1059: `consumers[outPinId].push_back(...)`, 1077: `auto ref = std::make_shared<RefSource>(srcNode->dspSource);`, 968: `src = std::make_shared<RefSource>(src, true);`, and per pin per link linear scans 948-957 (`for (auto& pin : node.inputs) for (auto& link : s_links) ... find_node_for_pin(otherPinId); find_pin(otherPinId)`), 1034-1057 (five linear find_pin/find_node_for_pin per link). The callback takes the same lock blocking: 3864 `std::lock_guard<std::mutex> lock(g_audioMutex);` (AUDIO_BUF_FRAMES = 512 at 3551, ~10.7 ms budget).`

Why: Every link create/delete (1179, 14354, 14377), every Properties pin edit (9345 calls `update_node_dsp(*node)` even though 9343 already did `pin.constantSrc->set(...)`), and every paste/convert re-wires under the lock while the stream is live; the audio thread stalls for the full duration, which grows with nodes x pins x links and includes heap allocation and every node's set_param/apply_config. This is the UI-side half of a per-block render path that locks and does unbounded work — the project's own hard rule.

Recommendation: Build the new wiring off-lock (compute the consumer map and RefSource wrappers into a staged structure, or into a cloned DSP graph), then swap pointers under the lock in O(1); or stop the streams before structural rewires exactly as delete_node already does via stop_streams(). Drop the redundant update_node_dsp on constant-value edits (the ConstantSource is already updated in place). Pair with a try_lock-and-output-silence policy in the callback (section 3).


## F351 [UNVERIFIED] medium (reporter: medium) workaround-hack — tools/mforce_ui/main.cpp:1081

**update_all_dsp swallows every exception, silently accepting a half-wired graph**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `1024 `try {` ... 1081-1083 `} catch (...) {\n        // Don't crash the UI on DSP wiring errors\n    }`. The wiring inside is set_param/add_param/clear_param on engine objects (941, 970, 972, 1078) plus Envelope apply_config (979).`

Why: If any node's set_param throws partway, the graph is left with some consumers wired and others at their constants, no status message, no log — exactly the 'lying wire' state the UI's own comment at 868-873 says it exists to prevent; and the catch(...) also hides programming errors (bad_alloc, logic_error) during development.

Recommendation: Catch `std::exception` only, report via transport_set_status(..., true) and stderr with the node/pin, and leave the graph in a defined state (re-run the first pass to constants on failure). Better: make engine set_param non-throwing for unknown names (it already is for svf_source.h 65-69) and remove the try entirely.


## F352 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:1114

**delete_node removes links and the node but never re-wires DSP, so consumers keep pulling the deleted node's dspSource; the continuous stream then plays a graph the canvas no longer shows**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `1120-1129 `s_links.erase(std::remove_if(...))`, 1167-1170 `s_nodes.erase(...)` — no update_all_dsp, unlike delete_link at 1179 `update_all_dsp();`. Consumers were wired by 472 `dspSource->set_param(pinName, src);` holding a shared_ptr, so the deleted source stays alive and connected. Callers 14501 `for (int nid : sel) delete_node(nid);` and 11567 `delete_node(s_contextNodeId);` do not re-wire either; play_continuous (4873 `find_output_source()`, 4878 `prepare_graph(samples)` which only calls prepare at 4204) streams the raw dspSource graph. Also: comment 1140-1146 says the dynamic-pin entry 'is dropped' but 1157 does `it.value() = nullptr;  // pin stays promoted, unwired`, inside an erase-shaped loop (1149-1162) whose both branches `++it`.`

Why: After Delete on, e.g., an LFO feeding a filter, the Properties pane shows the pin unconnected at its constant while the live stream still hears the LFO until some unrelated link edit or load happens to call update_all_dsp — a UI/DSP divergence of exactly the 'lying wire' kind the pin_type_compat_error comment says the UI must prevent. The vestigial loop shape and contradicting comment make the function's intent unreadable.

Recommendation: Call update_all_dsp() at the end of delete_node (or make the Delete callers do it once per batch); collapse the two sequential `for (auto& node : s_nodes) { if (node.id != nodeId) continue; ... break; }` lookups (1118, 1134) into one find; rewrite 1149-1162 as a plain range-for and fix the comment to match the 'unwired, still promoted' behavior.


## F353 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:1258

**save_file_dialog / open_file_dialog are verbatim specializations of text_save_dialog / text_open_dialog — four copies of the OPENFILENAMEA setup**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `1258-1273 `save_file_dialog()`: `char filename[MAX_PATH] = "patch.json"; OPENFILENAMEA ofn{}; ... ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0"; ... ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR; ofn.lpstrDefExt = "json"; if (GetSaveFileNameA(&ofn)) { remember_feature_dir("save", filename); return filename; }` is byte-for-byte `text_save_dialog("save", jsonFilter, "json", "patch.json")` (1294-1311). Likewise 1275-1289 `open_file_dialog()` == `text_open_dialog("open", jsonFilter)` (1313-1327).`

Why: 70 lines where 10 would do; the two JSON dialogs cannot pick up a fix made to the generic ones (e.g. a future switch to IFileDialog, which the file already uses for folders per line 14).

Recommendation: `static constexpr const char* kJsonFilter = "JSON Files\0*.json\0All Files\0*.*\0"; static std::string save_file_dialog() { return text_save_dialog("save", kJsonFilter, "json", "patch.json"); } static std::string open_file_dialog() { return text_open_dialog("open", kJsonFilter); }` and make the feature key an enum rather than a string (settings_dir_slot 2465-2474 returns nullptr silently on a typo).


## F354 [UNVERIFIED] high (reporter: high) duplication — tools/mforce_ui/main.cpp:1368

**The patch JSON format has four hand-maintained codecs (engine loader, UI loader, two UI savers) plus a third clipboard schema; jsonExtras carry-through is the symptom**

Reporters: arch-ui-tools

Evidence: `UI envelope_stages_from_json (1368-1387) is line-for-line the engine's stage parse (patch_loader.cpp 680-699: `s.ramp.startVal = sj.value("startVal", 0.0f); ... s.nominal = sj.value("nominal", 0.0f); env->add_stage(s);`). UI enum-label matching 1892-1912 vs engine 389-415 (same 'enum string matches no label; keeping default' message). UI CurveNode knots/interp/mode/exprKnots 1794-1821 vs engine 732-751. UI instrument block 2124-2143 vs engine 1516-1528 and again 1841-1850. UI ref/tap/multi link building 2051-2092 vs engine resolve_param 105-139. Writers: serialize_patch_graph (2806-3268) and save_node_graph (3275-3463) are twins — save_node_graph 3408 'Re-emit the loaded seed (see save_patch_graph)', 3433 'Groups — same engine-blind section as the patch save'. clip_node_state/clip_instantiate (10286-10404) is a third node schema ('pins', 'settings', 'arrays', 'formants' as 4-tuples, 'chPins'). GraphNode::jsonExtras (296, 1937-1981, 3414-3422) exists to carry 'params the UI does not model' so save cannot drop them.`

Why: Every new JSON key must be taught to four parsers/writers or it is silently dropped — the repo's own history (backlog 3n, the 2026-08-10 damper/gain loss, the 2026-08-13 volume-drop bug, all cited in comments) is a list of exactly that failure. jsonExtras is a workaround for not having one codec.

Recommendation: Introduce one typed patch document (e.g. mforce/render/patch_json.h: NodeSpec{id,type,params,dynamicPins,seed,...}, InstrumentSpec, GroupSpec, UiLayout) with a single from_json/to_json used by the engine loader, the UI loader, both UI savers and the clipboard. The UI then holds NodeSpec + editor decorations and never re-interprets JSON keys; jsonExtras disappears because unmodeled keys stay in the typed document.


## F355 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:1368

**Envelope stage-list parsing exists in the UI and the engine loader but not in envelope_json.h; the UI serializer has no engine twin at all**

Reporters: arch-duplication

Evidence: `main.cpp envelope_stages_from_json 1368-1387 and engine/src/patch_loader.cpp:680-700 are the same 20-line stage parse (`s.ramp.type = (t == "Expo") ? RampType::Expo : (t == "InverseExpo") ? ...`); envelope_json.h:15 states "Stage-list form ({"stages": [...]}) is handled" elsewhere; main.cpp envelope_stages_to_json 1338-1365 is the only serializer (the engine cannot write stages).`

Why: The preset form was moved to envelope_json.h precisely because the hand-copied dispatch drifted (main.cpp:1857-1864 records the damper-fires-on-attack bug); the stage-list form is in the same pre-drift state today. `nominal` (1361, 1384) was already missed once ("never taught to this serializer", 1356-1360).

Recommendation: Move `envelope_from_stages_json` / `envelope_stages_to_json` into envelope_json.h next to the preset dispatch; patch_loader.cpp and the UI call them.


## F356 [UNVERIFIED] high (reporter: high) soundness — tools/mforce_ui/main.cpp:1561

**File > Open / Ctrl+O has no exception boundary: a malformed or non-patch JSON exits the process; a structural failure leaves an empty graph aimed at the bad file**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `1568: `json root = json::parse(f);` (throws parse_error) ... 1576-1588: `s_nodes.clear(); s_links.clear(); ... s_nextId = 1;` ... 1610: `std::string outputId = root["graph"]["output"].get<std::string>();` (throws type_error when absent). 2419-2424 `load_graph()` calls it bare; callers 13664 (`if (ImGui::MenuItem("Open", "Ctrl+O")) { load_graph(); }`) and 13829 have no try; the only catch is main()'s outer one at 14649 which `return 1`s.`

Why: Opening a scores/*.json, mforce_ui_settings.json or any truncated file from the File menu terminates the editor and loses unsaved work — a public-release embarrassment. Even where a caller does catch (Open Recent, 13691; CLI arg, 13538), the function has already torn down the graph (1576-1588) and set `s_currentFilePath = path; s_graphDirty = false;` (1573-1574) before validating structure at 1609-1610, so the user is left with an empty canvas whose Ctrl+S (`save_graph` 3534-3538 writes `s_currentFilePath` unconditionally) overwrites the file they tried to open.

Recommendation: Validate the document fully (parse, `graph.nodes` array, `graph.output` string) into locals BEFORE touching any s_/g_ state, then commit; wrap `load_graph()` (and the two menu/shortcut callers) in the same try/catch + transport_set_status pattern the recents path already uses. Consider making load_graph_from_path return bool/expected instead of throwing through UI code.


## F357 [UNVERIFIED] high (reporter: high) god-class — tools/mforce_ui/main.cpp:1561

**load_graph_from_path is a ~800-line, nine-phase god function with dead state and an embedded layout algorithm**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `1561 `static void load_graph_from_path(const std::string& path) {` ... 2359 `}`. Phases: formant pre-scan 1621-1687, perf-node consumption 1696-1703, node creation + six type-specific param branches 1705-1994, Note-face assembly 1996-2044, link pass 2046-2117, output/instrument block 2119-2156, groups 2158-2192, position restore 2194-2227, 100-line control-strip layout 2228-2329, panning 2338-2345. Dead: 1613 `std::unordered_map<std::string, GraphNode*> nodeMap;` is written at 1751 `nodeMap[id] = &gn;` and never read. 1643 `std::unordered_map<std::string, bool> ownedFormants;` duplicates the 1443 `std::unordered_set<std::string> ownedFormants;` scan in convert_parammap_to_wiring with different criteria.`

Why: No phase can be tested, reused (the clipboard/paste and replace paths re-implement pieces) or reasoned about in isolation; the function mixes document validation, model construction, DSP configuration and canvas layout, and dead locals like nodeMap (which would dangle on s_nodes reallocation if ever used) accumulate because nothing is small enough to notice.

Recommendation: Split into pure phase functions over a parsed document: `consume_owned_formants(doc)`, `build_nodes(doc)`, `restore_node_params(GraphNode&, const json&)` (with per-type hooks for Envelope/CurveNode/FormantSpectrum/NameGate), `build_links(doc, pinMaps)`, `restore_groups`, `restore_positions`, `layout_control_strip(unplaced)`. Delete nodeMap. Share one owned-formant predicate with convert_parammap_to_wiring.


## F358 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:2366

**Recents/settings are persisted CWD-relative although documented as 'next to the exe', and recent-path dedupe is case-sensitive on a case-insensitive filesystem**

Reporters: tools-ui-chunk1-L1-2488

Evidence: `2366 `static const char* RECENTS_PATH = "mforce_recents.json";`, 2444 `static const char* SETTINGS_PATH = "mforce_ui_settings.json";` vs 2428 comment 'Tiny JSON next to the exe'. 2378-2388 normalize_path only swaps '\\' for '/', then 2401/2412 use `std::find` equality.`

Why: Launching from a different working directory (a worktree, a shortcut, the CLI harness) silently starts with empty recents and forgotten folders; `C:/x/a.json` and `c:/x/a.json` appear as two recent entries.

Recommendation: Resolve both paths against the executable directory (GetModuleFileNameA → parent_path) or %LOCALAPPDATA%/MForce; lowercase (or use std::filesystem::equivalent) for dedupe.


## F359 [UNVERIFIED] high (reporter: high) performance — tools/mforce_ui/main.cpp:2499

**topo_sort is a 5-deep nested scan and runs every frame per drawn group via group_output_node**

Reporters: tools-ui-chunk2-L2489-5005

Evidence: `2502-2519: `for (auto& node : s_nodes) { ... for (auto& pin : node.inputs) { for (auto& link : s_links) { ... for (auto& srcNode : s_nodes) { for (auto& sp : srcNode.outputs) {` — O(N·Pin·L·N·Pout); 2533-2534 the visit lambda re-scans s_nodes per node. group_output_node 2691 `for (auto* n : topo_sort())` is called from draw_group_node 9056 (every frame, every visible group, right after draw_group_node already ran group_boundary itself at 9055 — group_output_node runs it again at 2685) and from the breadcrumb at 13876 every frame. group_boundary 2645-2649 does four linear `find_node_for_pin`/`find_pin` lookups per link (each O(N·P), 806-820).`

Why: Per-frame cost scales roughly quadratically in nodes and pins times links. The library patches already have ~27 nodes with multi-pin nodes (Mixer/Partials); grouped patches are the direction the editor is going (oboe_grouped, clarinet_attempt cited at 3237). This is UI-thread time stolen from the frame loop that also services voice_gc and the audio watchdog, and it grows with exactly the patches the editor exists for.

Recommendation: Build a pin->node/pin index (unordered_map<int, {GraphNode*, Pin*}>) once per graph edit (key on g_graphEditCounter, which already exists for the instrument cache) and use it in topo_sort, group_boundary and all find_source_* helpers. Cache the sorted order and each group's boundary/output node on the same key so draw_group_node and the breadcrumb pay a map lookup per frame instead of a full sort.


## F360 [UNVERIFIED] low (reporter: low) duplication — tools/mforce_ui/main.cpp:2547

**Six copies of the same input-pin -> source-output link walk**

Reporters: tools-ui-chunk2-L2489-5005

Evidence: `find_source_pin 2547-2557, find_source_node 2559-2574 (== find_source_pin(id).first), find_source_out_pin 2578-2589, inline in topo_sort 2505-2519, inline in both multi-pin loops 2949-2957 and 3316-3325, and find_output_source 4185-4194 — all `if (link.endPinId == X) other = link.startPinId; if (link.startPinId == X) other = link.endPinId;` followed by a scan of s_nodes/outputs.`

Why: Each copy is O(L·N·P) and each re-derives the 'links may be stored in either direction' rule; a fix to one (e.g. the isTap exclusion only topo_sort knows about, 2517) does not reach the others.

Recommendation: One `resolve_source(int inputPinId) -> std::pair<GraphNode*, Pin*>` over the per-edit pin index from the topo_sort finding; derive the three helpers from it or delete them.


## F361 [UNVERIFIED] low (reporter: low) duplication — tools/mforce_ui/main.cpp:2547

**UI graph-walk helpers triplicated; group-ancestor walk and wormhole ghost drawing copied inline**

Reporters: arch-duplication

Evidence: `find_source_pin 2547-2557, find_source_node 2559-2574, find_source_out_pin 2578-2589 are the same link walk returning different members; "walk up to the visible ancestor group" loop at project_pin 9122-9135, project_entity 13997-14004, draw_span_for 14081-14088 and the hover block 14123-14129; ghost line + rect drawing draw_span_for 14090-14099 vs 14131-14144 verbatim; directory scan audition_refresh_target_list 5292-5313 vs audition_load_folder 5315-5329; Win32 OPENFILENAME blocks ×5 (1258-1273, 1275-1289, 1294-1311, 1313-1327, 8500-8516) where text_save/open_dialog already generalise the rest; crash_log_stack 12796-12829 vs seh_crash_filter 12841-12885 duplicate SymInitialize/SYMBOL_INFO setup.`

Why: ~120 lines; each walk re-derives the link orientation rules (start/end can be either kind) that the rest of the file depends on being identical.

Recommendation: One `resolve_source(inputPin) -> {node, pin}` used by all three; `visible_ancestor_entity(label)`; call draw_span_for from the hover path; `list_files(folder, ext)`; route all dialogs through text_save/open_dialog.


## F362 [UNVERIFIED] low (reporter: low) duplication — tools/mforce_ui/main.cpp:2759

**rename_node and rename_group duplicate the identifier validation rules**

Reporters: tools-ui-chunk2-L2489-5005

Evidence: `2759-2762 and 2778-2781 both check empty / contains '.' / starts with "__" with the same error strings; 2763-2766 vs 2782-2786 both scan s_nodes and s_groups for collisions with the self-exclusion differing only in which container holds `this`.`

Why: sanitize_unique_id (2595-2610) encodes the same rules a third way (replace '.', strip '__'); three encodings of the id grammar will diverge when the next reserved prefix is added.

Recommendation: One `validate_identifier(name, err)` plus one `name_in_use(name, excludeNode, excludeGroup)`; have sanitize_unique_id call the same predicate.


## F363 [UNVERIFIED] medium (reporter: medium) god-class — tools/mforce_ui/main.cpp:2806

**serialize_patch_graph is 460 lines with eight phases and a side effect on the model**

Reporters: arch-duplication, tools-ui-chunk2-L2489-5005

Evidence: `2806-3266 one function: id assignment (2811-2819), Note-face synthesis (2821-2848), output resolution (2850-2860), paramMap remap closures (2862-2899), node emission with CurveNode/Envelope/NameGate/FormantSpectrum special cases (2901-3115), instrument block (3136-3173), score (3175-3199), groups (3201-3213), ui layout + noteFaces (3215-3262). 2843 `node.perfFieldIds[p.name] = fid;   // stable on the next save` mutates the GraphNode from inside the serializer, which is also invoked from get_cached_instrument (4638) and generate_unified (4351).`

Why: The function is the file format; every format change lands here, and the per-type `if (node.typeName == "X")` ladder (2921, 3025, 3038, 3046) grows with each node kind. Mutating perfFieldIds during a playback-cache rebuild means 'play a note' can change what the next Save writes, which is surprising for anyone debugging round-trip diffs.

Recommendation: Split into phase helpers (assign_ids, emit_perf_faces, emit_nodes, emit_instrument, emit_score, emit_ui) sharing a small SerializeContext; move the perfFieldIds assignment into an explicit `assign_perf_field_ids()` step so the serializer is const over s_nodes; consider a per-type emit hook on the registry entry instead of the typeName ladder.

Also reported as: serialize_patch_graph and save_node_graph duplicate ~110 lines of per-node emission


## F364 [UNVERIFIED] medium (reporter: medium) god-class — tools/mforce_ui/main.cpp:2806

**14,658-line single translation unit with 349 file-scope statics and three parallel node serializers plus two clipboard systems**

Reporters: arch-modern-cpp

Evidence: `serialize_patch_graph 2806 and save_node_graph 3275 emit the same per-node blocks (3028 `jnode["params"]["stages"] = envelope_stages_to_json(*env);` / 3068 formants / 3101 seed vs 3349 / 3380 / 3411); clip_node_state 10286-10331 is a third serializer; s_clipboard (10276, in-process JSON) and copy_selection_to_clipboard (11273, OS clipboard fragment) are two independent copy/paste implementations wired to different menus (Edit menu 13738-13743 vs context menu 11542 / Ctrl+C 14255); eval_map_curve 4484 mirrors engine curve evaluation`

Why: Node state has to be kept consistent in 3 serializers and 2 loaders; the duplicated clipboard paths already diverge in capability (one handles groups/positions, one skips Note/Output). The file size defeats incremental builds and review.

Recommendation: Split into graph_model (GraphNode/Link/groups + one serializer with a mode flag), audio_engine_bridge (callback, voice pool), editor (imnodes), panels; delete the s_clipboard path in favour of the saver-backed fragment path; have the UI call the engine's Curve::eval instead of mirroring it.


## F365 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:3024

**~130 lines of per-node emission copy-pasted between serialize_patch_graph and save_node_graph**

Reporters: tools-ui-chunk2-L2489-5005

Evidence: `Patch saver vs node saver, verbatim: multi-pin ref collection 2946-2969 vs 3314-3327; Envelope stages/timeMode 3024-3035 vs 3346-3356 (including the identical 4-line 2026-08-10 comment); FormantSpectrum child synthesis 3043-3069 vs 3358-3381; settingValues with the sustainLevel exclusion and its 5-line comment 3071-3088 vs 3383-3399; arrayValues 3090-3095 vs 3401-3406; seed 3097-3102 vs 3408-3412; jsonExtras 3104-3112 vs 3414-3422; groups block 3201-3213 vs 3433-3444; positions 3221-3233 vs 3446-3453.`

Why: Every emission rule fixed in one saver (sustainLevel exclusion, timeMode, extras-win-over-modeled) had to be re-applied by hand to the other; the file's own history (3n, 2026-08-10) shows these rules change. NodeGraph mode is still live (5 GraphMode::NodeGraph sites), so the copy is not dead, and a future divergence silently produces a node-graph file the loader rejects.

Recommendation: Extract `emit_node_body(GraphNode&, const IdMap&, UsedIds&, json& nodesOut) -> json` covering refs/envelope/formants/settings/arrays/seed/extras, plus `emit_groups()` and `emit_positions(sorted, ids)`. The two savers then differ only in the id exclusion set, the output/channel handling and the instrument/score block.


## F366 [UNVERIFIED] low (reporter: low) modern-cpp — tools/mforce_ui/main.cpp:3125

**Sample-rate and pi literals duplicated where named constants exist; two names for one sample rate**

Reporters: tools-ui-chunk2-L2489-5005

Evidence: `3125 and 3428 `root["sampleRate"] = 48000;`, 3592 `g_outputSpectrumSR = 48000`, 3806 `kMaxRingSec * 48000.0f` while AUDIO_SAMPLE_RATE (3547) and DSP_SAMPLE_RATE (230) both exist and are used interchangeably (4202 vs 4877); 3966-3967 `std::cos(t * 0.5f * 3.14159265358979323846f)` instead of std::numbers::pi_v<float>, recomputed per sample per channel even when pan is a constant.`

Why: The rate-bake audit the callback comment cites (3902, backlog 69) is exactly the class of bug duplicated literals cause; two constants for one value invites the UI and engine rates to drift apart silently.

Recommendation: One `kSampleRate` used by both savers and the ring budget; `std::numbers::pi_v<float>`; compute the pan gains only when the pan source changed (or accept the trig and note it).


## F367 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:3268

**Save ignores write failure and then clears the dirty flag**

Reporters: tools-ui-chunk2-L2489-5005

Evidence: `3270-3272 `std::ofstream f(path); f << root.dump(2); f.close();` (same at 3460-3462) — no is_open()/good() check, void return; save_to_path 3470-3472 then does `s_currentFilePath = path; s_graphDirty = false; recents_push(path);` unconditionally.`

Why: A locked file, read-only folder or bad dialog path produces no file, marks the document clean (so the close-prompt state machine at 802-804 lets the window close) and adds a non-existent path to recents. That is silent data loss on the one operation that must not fail silently.

Recommendation: Return bool from save_patch_graph/save_node_graph (check `f` after the write), keep s_graphDirty on failure, and report via transport_set_status; only push recents on success.


## F368 [UNVERIFIED] low (reporter: low) duplication — tools/mforce_ui/main.cpp:3477

**Two inconsistent lists of which types are Partials/Formant implementors**

Reporters: arch-duplication

Evidence: `is_partials_type 3477-3479 `t == "FullPartials" || t == "SequencePartials" || t == "ExplicitPartials"` and is_formant_type 3480-3483 (no FixedSpectrum) vs the link-compat lambdas is_ipartials 882-885 (adds CompositePartials) and is_iformant 877-881 (adds FixedSpectrum).`

Why: A CompositePartials node gets a time-domain strip instead of a bar chart and a FixedSpectrum node gets no gain-curve strip, purely because two hand lists diverged.

Recommendation: One predicate pair, ideally answered by the engine (`dynamic_cast<IPartials*>`/`IFormant*` on the dspSource, or a registry trait) rather than by name lists.


## F369 [UNVERIFIED] medium (reporter: medium) smell — tools/mforce_ui/main.cpp:3502

**~150 lines of retired render-path code still compiled: get_playback_patch_path, voice_schedule, apply_perform_nodes, apply_param_map, eval_map_curve/vcurve**

Reporters: tools-ui-chunk2-L2489-5005

Evidence: `get_playback_patch_path 3502-3526 (writes a temp JSON on every call) has no callers — grep shows only the definition; the 2026-09-13 unification comment at 4282-4284 says 'no temp file'. voice_schedule 3812-3820: definition only. apply_perform_nodes 4527-4538, apply_param_map 4540-4582, eval_map_curve 4484-4503, eval_map_vcurve 4505-4519: no callers; the comment at 4584-4585 says their consumer render_waveforms was deleted 2026-09-13. The stale comment at 8304 still describes get_playback_patch_path as the authoritative route.`

Why: eval_map_curve is a hand-rolled mirror of the loader's CurveNode chain semantics (patch_loader.cpp 1197-1218) with its own loglog branch — the file's comment at 4476-4479 even promises it 'mirrors the engine exactly'; dead mirrors of engine logic are the kind of thing that gets revived and then drifts. The temp-file writer also writes to a fixed `mforce_playback.json` name that two running UIs would clobber.

Recommendation: Delete all six functions and fix the 8304 comment; the unified Generate and the cached-instrument path are the only routes that should exist.


## F370 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:3541

**Stale banner describes the retired waveOut design; forward declarations scattered; dead default drum-map path**

Reporters: tools-ui-chunk2-L2489-5005

Evidence: `3541-3545 banner: 'Audio: poll-driven streaming via waveOut ... no callbacks, no threading issues' directly above the RtAudio callback design (3642-3646). transport_set_status is forward-declared three times (1392, 4077, 4279); init_audio is declared at 4011 and defined two lines later at 4013. TransportState 3624 default `drumMap[512] = "KK=patches/kick_drum.json;SN=patches/snare_drum.json"` names files that do not exist (patches/ was reorganized into library/<family>/; glob finds no patches/kick_drum.json).`

Why: The banner says the opposite of the threading model a reader must understand before touching anything in this range; the default drum map fails on first use of Drums mode.

Recommendation: Replace the banner with the 3642-3646 text, collect forward declarations in one block at the top of the audio section, and either point the default at a library path or leave it empty.


## F371 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:3541

**Stale section header still documents the removed waveOut poll design ('no callbacks, no threading issues')**

Reporters: arch-render-pipeline

Evidence: `3541-3545: `// Audio: poll-driven streaming via waveOut // Buffers are filled from the main loop each frame — no callbacks, // no threading issues, no deadlocks on quit.` directly above the RtAudio block (3642-3647) that documents the opposite.`

Why: The first thing a reader sees about the audio subsystem is a false statement about its threading model, in the exact area where threading is the risk.

Recommendation: Delete the waveOut header or replace it with a one-paragraph statement of the actual contract (RtAudio thread, what g_audioMutex guards, what the callback may touch).


## F372 [UNVERIFIED] low (reporter: low) elegance — tools/mforce_ui/main.cpp:3755

**voice_schedule_unlocked takes 10 positional parameters (6 defaulted) for data the engine already packages as StreamingVoice**

Reporters: tools-ui-chunk2-L2489-5005

Evidence: `3755-3764 signature `(patch, source, totalSamples, gain, midiNote, bool held = false, envs = {}, int poolSlot = -1, performSource = nullptr, advanceList = {})`; call sites pass `ip, sv.source, sv.durSamples, sv.gain, int(noteNum), false, {}, slot, sv.performSource, sv.advanceList` (4697-4699) and `..., INT_MAX / 2, sv.gain, int(noteNum), true, std::move(envs), slot, sv.performSource, sv.advanceList` (4803-4805). instrument.h 246-264 StreamingVoice already carries source/durSamples/gain/performSource/advanceList.`

Why: Positional bool/int/empty-brace arguments are easy to transpose (held vs poolSlot) and the two call sites copy the advanceList vector instead of moving it.

Recommendation: `voice_schedule_unlocked(std::shared_ptr<InstrumentPatch>, PitchedInstrument::StreamingVoice&&, int poolSlot, int midiNote, HeldState{bool held; std::vector<Envelope*> envs})` — move the StreamingVoice in.


## F373 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:3827

**Voice stealing is a hard cut with no declick, and a 'self-heal' masks an acknowledged slot-accounting bug**

Reporters: arch-render-pipeline

Evidence: `acquire_pool_slot 3831-3839: picks the active voice of this instrument closest to done and `voice_deactivate_unlocked(g_voices[best])`, then `prepare_voice_at(slot, ...)` re-initialises the same graph for the new note; voice_schedule_unlocked 3781-3791 does the same for the 16-entry UI table. Neither path uses the fade machinery that already exists (`fadeGain/fadeStep`, used for the 3 ms detached cut at 4776-4780). 3838: `else pitched->release_all_voices();  // slot leak (bug) — self-heal`.`

Why: Deactivation stops the stolen voice's contribution at an arbitrary sample and prepare() resets its state, so a steal under polyphonic playing is a step discontinuity (click) — the same artifact class the project spent backlog 63/63b removing from note ends. The release_all_voices fallback silently resets pool bookkeeping instead of fixing the leak it names, so a real accounting bug would present as a surprise cut of every other sounding note on that instrument.

Recommendation: Give each pool slot a short crossfade on steal: keep the stolen voice sounding into a per-slot fade buffer (or a spare 'releasing' Voice entry that owns a copy of the last N ms), or at least apply the existing fadeStep over ~3 ms before re-preparing. Remove the self-heal and assert/log the leak so it gets found.


## F374 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:3842

**UI thread reads audio-thread-written state (voice.active, midiNote, g_streamSource, g_bufferPlayback) without the mutex or atomics**

Reporters: arch-render-pipeline, tools-ui-chunk2-L2489-5005

Evidence: `3842-3851 `any_voice_active()`/`is_playing()` read `g_voices[i].active`, `g_streamSource`, `g_bufferPlayback` unlocked; 7941-7944 keyboard highlight `if (g_voices[vi].active && g_voices[vi].midiNote == midi)` unlocked. The callback writes these under the lock: `v.active = false` (3749 via 3924), `g_bufferPlayback = nullptr` (3935), `g_streamSource = nullptr` (3944).`

Why: Concurrent non-atomic read/write is a data race (UB under the C++ memory model); on MSVC x64 it is benign in practice today, but it is the kind of thing a sanitizer or a future compiler optimisation (hoisting the load out of a loop) turns into a stuck 'playing' state. The code elsewhere is careful about this (peak meters and heartbeat are atomics), so this is an inconsistency rather than a design choice.

Recommendation: Make `Voice::active` a std::atomic<bool> (relaxed) and `midiNote` an atomic<int>, and have the callback publish `g_streamSource`/`g_bufferPlayback` completion through an atomic flag; or route is_playing()/isNoteActive through a snapshot taken once per frame under the lock (which goes away once finding 1 is fixed).

Also reported as: UI thread reads audio-thread-written non-atomics without the lock (is_playing, any_voice_active)


## F375 [CONFIRMED-BY-MATT'S-CLAUDE] critical (reporter: critical) rt-safety — tools/mforce_ui/main.cpp:3864

**Audio callback blocks on g_audioMutex while the UI thread holds it across allocating, O(graph) note-on and rewiring work**

Reporters: arch-build-headers, arch-modern-cpp, arch-render-pipeline, arch-ui-tools, tools-ui-chunk2-L2489-5005

Evidence: `audio_callback: `std::lock_guard<std::mutex> lock(g_audioMutex);` (3864) for the whole 512-frame buffer. Same mutex held by the UI thread across: play_note 4676-4699 (`collect_envelopes(...)` dynamic_cast walk + vector push_back, `pitched->prepare_voice_at(...)` = `vg.source->prepare(ctx, durSamples)` over the entire voice graph, and `sv.advanceList` copied by value into voice_schedule_unlocked at 4699); play_note_held 4749-4762 -> continue_voice_live -> deliver_continuation (instrument.h:475 `for (auto& [nid, src] : vg.nodesById) src->reseed();`, 478 `make_glide` = `std::make_shared<Envelope>` + two `add_stage` push_backs + prepare) and 4788-4805; update_all_dsp 1023-1059 (`std::make_shared<RefSource>(src, true)` at 968, unordered_map build at 1032); set_setting under lock at 9521-9523 whose own comment says it 'can rebuild internal arrays (ExplicitPartials)'; per-note `MultiplexSource::set_clone_param` (multiplex_source.h:100 `pendingCloneParams_[nodeId + "." + paramName] = ...`) reached from instrument.h:344/493.`

Why: This is the project's own rule (no locks or unbounded work on the render path) violated at the single most important point. A plain std::mutex gives the RtAudio thread no priority inheritance on Windows, so every key press, legato continuation, link edit or setting tweak stalls the callback for the full duration of the UI-side work; the period budget is 10.7 ms (AUDIO_BUF_FRAMES=512 @ 48 kHz, 3549-3551), so any prepare() of a heavy graph (Multiplex rebuild_ runs the std::function builder for every clone if templateDirty_ is still set at the first prepare, multiplex_source.h:106/134-151) produces an underrun exactly when the user is playing. The cost is not measured here, but the mechanism is unconditional.

Recommendation: Make the audio thread lock-free: the callback should try_lock (and output silence/last block on contention) at minimum; properly, move all note-on/rewire work off the shared mutex by preparing voices on the UI thread into slots the callback does not currently own and publishing them through a SPSC command queue (note-on/off, pointer swaps for rewires), with the callback consuming commands at block start. prepare()/collect/alloc then never run while the callback waits. Keep voice_gc as is.

Also reported as: Audio callback blocks on g_audioMutex while the UI thread holds it across whole-graph prepare, heap allocation and shared_ptr destruction | Audio callback blocks on a std::mutex that the UI thread holds during heap allocation and voice preparation | Audio callback blocks on g_audioMutex that the UI thread holds across graph rebuilds and voice preparation | Audio callback takes a std::mutex per block that the UI thread holds across a heap-allocating voice prepare


## F376 [UNVERIFIED] medium (reporter: medium) performance — tools/mforce_ui/main.cpp:3880

**Pure per-sample granularity end to end: no block path in ValueSource, 16-slot active scan per sample in the callback, render_chunk called with n=1 per sample offline**

Reporters: arch-render-pipeline

Evidence: `ValueSource exposes only `virtual float next()` (dsp_value_source.h:78); the callback's inner loop (3880-3927) runs `for (int v = 0; v < MAX_VOICES; ++v) { if (!voice.active) continue; ... }` per frame plus the ring follower and advanceList loop per voice per sample; the node-graph stream does `std::cos`/`std::sin` per sample per channel (3966-3967). Offline, play_note and finish_line call `render_chunk(vg, buf, vIdx, startFrame, 1, gain)` per sample (631, 686), each doing `buf.resize(base + size_t(n))` and re-evaluating `capturing`.`

Why: Every sample is a chain of virtual calls through the whole graph for every voice (times Multiplex clone count), with no opportunity for SIMD or cache-friendly block processing; the per-sample bookkeeping in the callback and the n=1 render_chunk calls add overhead on top of the graph cost. This is the structural ceiling on polyphony and on how heavy a patch can stream, and it is what makes the audio lock stall (finding 1) hurt more.

Recommendation: Short term: build a per-block active-voice list once per callback, hoist pan trig to when pan changes, and have play_note/finish_line render the tail in chunks (render_chunk already supports n>1). Longer term: add `virtual void next_block(std::span<float>)` to ValueSource with a default implemented via next(), and port the hot leaf/filter nodes incrementally.


## F377 [CONFIRMED-BY-MATT'S-CLAUDE] high (reporter: high) duplication — tools/mforce_ui/main.cpp:3963

**UI re-implements StereoMixer's equal-power pan twice and has already drifted from mixer.cpp (missing unity-center normalisation)**

Reporters: arch-duplication, arch-render-pipeline

Evidence: `main.cpp:3965-3967: `float t = (p + 1.0f) * 0.5f; float aL = std::cos(t * 0.5f * 3.14159265358979323846f); float aR = std::sin(t * 0.5f * 3.14159265358979323846f);`  (comment 3953: "per-sample mirror of the engine's StereoMixer::render"); same again at 13409-13412 in --dump-stream.  engine/src/mixer.cpp:39-42: `constexpr float kRoot2 = 1.41421356237309504880f; float aL = kRoot2 * std::cos(t * 0.5f * 3.14159265358979323846f); float aR = kRoot2 * std::sin(...)` with comment "normalized to UNITY at center (Matt 2026-09-14 ...)".`

Why: The engine pan law was changed on 2026-09-14 to unity at center; the two UI copies still use the old -3 dB-center law, so a node graph streamed live in the UI is 3 dB quieter at center than the CLI/WAV render of the same graph — exactly the loudness mismatch the mixer comment says was fixed. The whole stream inner loop (gains, pan, soft_clip) is also copied from audio_callback into --dump-stream (13389-13415, ~25 lines), so any further mixer change needs three edits.

Recommendation: Expose `equal_power_pan(float pan, float& aL, float& aR)` from render/mixer.h and call it from StereoMixer::render and the UI; factor the per-frame stream mix into one `mix_stream_frame()` used by audio_callback and --dump-stream.

Also reported as: UI audio callback re-implements StereoMixer's pan law and has drifted: node-graph live stream is 3 dB quieter than the WAV at center


## F378 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:4103

**audio_watchdog never retries once init_audio has failed, although its status messages promise it will**

Reporters: tools-ui-chunk2-L2489-5005

Evidence: `4103-4111: `if (beat != lastBeat || !g_audio) { lastBeat = beat; lastBeatTime = now; ... if (g_audio) return; }` followed by 4114 `if (now - lastBeatTime < 1.0) return;`. With g_audio null (every init_audio failure path resets it: 4030, 4050, 4062; shutdown_audio 4073) lastBeatTime is refreshed every frame, so the 1-second dead test can never pass. Yet 4097 reports 'watchdog will retry' and 4140 'Audio reopen FAILED ... — retrying...'. Startup failure at 13510-13512 is non-fatal and lands in the same state.`

Why: After a transient device failure (the exact WASAPI scenario the watchdog was written for) one failed reopen leaves the app permanently silent while the status bar says it is retrying; the user has no reason to restart.

Recommendation: Treat `!g_audio` as 'dead' rather than 'alive': only refresh lastBeatTime when the heartbeat moved, and let the backoff branch call init_audio when g_audio is null (keep the 2 s backoff so a missing device doesn't spin). Or, if no-retry is intended, remove the two 'retrying' strings.


## F379 [UNVERIFIED] medium (reporter: medium) workaround-hack — tools/mforce_ui/main.cpp:4208

**UI fakes infinite streaming by mutating engine Envelope objects (absolute_time flip, temporary hold stage, 2-hour prepare) — a documented UI-side workaround for a one-flag engine feature**

Reporters: arch-hacks-census, tools-ui-chunk2-L2489-5005

Evidence: `tools/mforce_ui/main.cpp:4218 `// Streaming fix, UI-side only (Envelope's public API, no engine change):` ... :4225-4226 `//  3. envelopes with no expand stage get a temporary hold stage appended so
//     they sustain their final value instead of cutting to 0.` :4229-4230 `// >2 h) needs an engine-side Envelope hold/loop mode — engine is owned by
// another agent this run.` :4232 `static constexpr int STREAM_PREP_SECONDS = 7200;``

Why: The comment records that the hack exists because of a one-run ownership boundary ('engine is owned by another agent this run'), not a design choice. IDEAS.md:131-133 confirms the real fix is 'one-flag change in envelope.h::next()'. The UI reaches into engine objects, appends stages, flips a timing mode, and must restore all of it on stop (`g_streamEnvHolds`), with a 2 h hard limit on streams — behaviour a user will hit.

Recommendation: Add the Envelope hold/loop flag in the engine (IDEAS entry), prepare streams with it, and delete StreamEnvHold/STREAM_PREP_SECONDS.

Also reported as: Streaming envelope hold is a 2-hour prepare + absolute_time flip + stage-list surgery, written around an engine gap the engine has since closed


## F380 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:4484

**Three UI mirrors of CurveNode::map, two of which already disagree**

Reporters: arch-duplication

Evidence: `eval_map_curve 4484-4503 (log-x interpolation with a `loglog` branch), eval_map_vcurve 4505-4519, curve_eval 7054-7068 (`// Mirror of CurveNode::map (LogX/LogLog) for the plot preview.` — but it has no loglog branch); engine curve_node.h `float map(float x) const` at 87 / map_expr 62. The UI's own comment at 7462-7463 says the plot is "evaluated through the ENGINE's own map() rather than a mirror of it — the node is right here, so there is nothing to keep in sync".`

Why: The stash-curve preview (curve_eval) and the per-note application (eval_map_curve) use different interpolation for loglog entries, so what the Mappings dialog plots is not what is applied. Three copies of a 15-line evaluator the engine already owns.

Recommendation: Build a `CurveNode probe` from the stash entry's knots/interp (as draw_curve_node already does at 7465-7467) and call `probe.map(freq)` in apply_param_map and draw_one_curve; delete the three mirrors.


## F381 [UNVERIFIED] medium (reporter: medium) performance — tools/mforce_ui/main.cpp:4629

**Instrument cache key is 'any edit', so every Properties value tweak forces a full serialize->parse->polyphony×build on the next key press**

Reporters: tools-ui-chunk2-L2489-5005

Evidence: `4630-4632 `if (g_cachedInstrument && g_cachedEditCounter == g_graphEditCounter && g_cachedTapNode == s_listenTapNode) return g_cachedInstrument;` — g_graphEditCounter is bumped by mark_graph_dirty (791-794), which every Properties InputFloat edit calls (9343-9346, 9375-9378). The rebuild is `serialize_patch_graph(...).dump()` -> `load_instrument_patch_json(..., LIVE_MIN_POLYPHONY)` (4638-4643), the '~0.6-1.3 s on heavy patches' cost the cache comment at 4602-4604 was introduced to remove.`

Why: The mission statement is 'tweak parameter, hear the results'; with the keyboard path the first note after each slider move pays the full rebuild, and get_cached_instrument also clears g_liveHeldKeys (4648), so a held legato line is dropped by a value edit. Only structural edits need a rebuild — the engine voices already expose set_param/set_setting by node id (used by push bindings, instrument.h 335-345).

Recommendation: Split the counter: structural edits (add/delete/link/type change) bump a rebuild key; value edits push the new constant/setting into every pool voice's nodesById entry for that node (and the Multiplex clones via set_clone_param) under the audio lock, leaving the cached instrument intact.


## F382 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:4716

**UI collect_envelopes re-walks the voice graph with dynamic_cast on every note-on (under the audio lock) although the loader already collects allEnvelopes per voice**

Reporters: arch-render-pipeline

Evidence: `main.cpp:4716-4733 `collect_envelopes(...)`: recursive walk over `input_descriptors()`/`param_descriptors()` with `dynamic_cast<mforce::RefSource*>`, `dynamic_cast<mforce::Envelope*>`, `get_param(d.name)` shared_ptr copies and `out.push_back`; called per key press at 4685 and 4796 inside the g_audioMutex scope. patch_loader.cpp:1551-1555 already builds `vg.allEnvelopes` for every voice at load (`for (auto& [nid, src] : g.valueNodes) if (auto* env = dynamic_cast<Envelope*>(src.get())) { vg.allEnvelopes.push_back(env); ...}`) and instrument.h:171-178 exposes it on `voicePool[slot]` with the Multiplex caveat already encoded as 'empty'.`

Why: Two definitions of 'the envelopes a note gates' (descriptor walk vs node table) can disagree, and the UI version runs a RTTI graph walk plus allocations for every note while the audio thread waits on the same mutex. The UI's own 2026-09-13 tap-loop bug fix (RefSource traversal) lives only in its copy.

Recommendation: Use `pitched->voicePool[slot].allEnvelopes` (and `topMultiplex` for the gateable test) in play_note/play_note_held; if the descriptor walk finds envelopes the node table misses, fix the loader's collection once and delete the UI walk. Keep --gatecheck as a test of the engine's list.


## F383 [UNVERIFIED] high (reporter: high) soundness — tools/mforce_ui/main.cpp:4872

**Dual DSP representation: the UI builds and streams its own DSP graph while Play/Generate use the engine-built instrument, so Stream and Play can render the same patch differently**

Reporters: arch-ui-tools

Evidence: `play_continuous: `ValueSource* src = find_output_source(); ... g_streamSource = src;` (4873-4883) streams the UI graph built by GraphNode::create_dsp (385-467: Envelope defaults to `Envelope::make_adsr(...)` 411, PerformNode is a stand-in `ConstantSource(440.0f)` 404-406) and wired by update_all_dsp (1022-1084, its own policy: 'first consumer keeps real source, rest get RefSource wrappers' 1062-1079). Play/keyboard/Generate instead use `load_instrument_patch_json(liveRoot.dump(), ...)` (4642-4643, 4352), whose wiring policy lives in patch_loader.cpp (promote_starved_refs 223/326, collect_advance_ids 184, tap binds 89-131). apply_param_map (4540-4582) and apply_perform_nodes (4527-4538) exist only to re-implement engine note-on semantics on the preview graph ('mirroring the engine's note-on application exactly (instrument.h map/vmap)' 4476-4477). A headless `--dump-playback` mode exists 'for numeric comparison against the CLI render of the same file (UI-vs-CLI sound-mismatch debugging)' (13246-13248).`

Why: Two renderers of one patch is the root of the recurring UI-vs-CLI sound-mismatch class the repo's own tooling is built to chase, and the UI-side graph is also the surface exposed to the RT races above. It also forces every edit to invalidate the engine instrument (serialize->parse->N-voice rebuild, 0.6-1.3 s on heavy patches per 4603-4604) because the UI cannot address the engine's objects directly.

Recommendation: Make the engine loader the sole DSP builder. Stream = a held voice (envelopes held as stream_envelopes_hold does today) of the cached engine instrument, exactly as Generate already uses the engine. Strip dspSource from GraphNode (keep descriptors + pin constants for the editor) and forward parameter edits by node id to the engine instrument's ConstantSources/settings (patch_loader.cpp already keys nodes by id in valueNodes, 674/712). This deletes create_dsp/update_all_dsp/apply_param_map/apply_perform_nodes/stream_envelopes_* (~450 lines) and closes the streaming race surface.


## F384 [UNVERIFIED] medium (reporter: medium) performance — tools/mforce_ui/main.cpp:5080

**MIDI input is polled once per UI frame at frame end, adding a frame of latency and freezing during synchronous Generate**

Reporters: arch-render-pipeline

Evidence: `pump_midi (5080-5122) drains `g_midiIn->getMessage(&msg)` in a loop; it is called from the main loop after draw at 14570, and the blocking `transport_generate()` runs on the same thread at 14633-14634. Note-on then goes play_note_held -> prepare under the audio lock (4788-4805). Comment at 5024-5027 confirms the polling design.`

Why: A hardware key press waits up to one frame period (16.7 ms at 60 Hz, longer with vsync stalls, unbounded while Generate or a file dialog blocks the main loop) before the note is even prepared, then up to another 10.7 ms audio period; MIDI events queued during Generate all fire afterwards as a burst. For a tool whose stated goal is real-time playing this is a visible latency floor that RtMidi's setCallback would remove.

Recommendation: Use RtMidiIn::setCallback and push decoded events into a lock-free queue consumed either by the audio thread (once note-on is callback-safe per finding 1) or by a high-priority dispatcher; at minimum pump MIDI before draw and also inside long UI-blocking operations.


## F385 [UNVERIFIED] low (reporter: low) efficiency — tools/mforce_ui/main.cpp:5095

**Mod-wheel / channel-pressure MIDI messages can trigger a full instrument rebuild on the UI thread**

Reporters: tools-ui-chunk3-L5006-7525

Evidence: `5096: if (auto ip = get_cached_instrument(); ip && ip->instrument)
5105: if (auto ip = get_cached_instrument(); ip && ip->instrument)
get_cached_instrument 4634-4656: when the cache is stale it does serialize_patch_graph(...) + load_instrument_patch_json(liveRoot.dump(), ...) and reports 'Instrument rebuilt: %d voices, %d ms'.`

Why: After any graph edit, the first wheel or aftertouch message (not a note) pays the full serialize->load rebuild (hundreds of ms historically) as a UI stall, and the value lands on the NEW instrument's InstrumentState while any still-sounding voices belong to the previous instrument, so the gesture is lost for them.

Recommendation: For CC1/0xD0 read g_cachedInstrument directly (no rebuild side effect) and skip when null; let note-on remain the only rebuild trigger.


## F386 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:5139

**apply_score_defaults has duplicated assignments and a leftover (void)0; from an incomplete edit**

Reporters: tools-ui-chunk3-L5006-7525

Evidence: `5140: g_transport.velocity = vel;
5141: g_transport.duration = dur;
5142: 
5143: g_transport.velocity = vel;
5144: g_transport.duration = std::clamp(dur, 0.05f, 30.0f);
5145: (void)0;`

Why: Lines 5140-5141 are dead (overwritten immediately), and the unclamped write at 5141 reads as if duration is intentionally stored raw; the stray (void)0 is edit residue. Harmless at runtime but it is exactly the kind of leftover that embarrasses a public read.

Recommendation: Keep only lines 5143-5144; delete 5140-5142 and 5145.


## F387 [UNVERIFIED] low (reporter: low) modern-cpp — tools/mforce_ui/main.cpp:5333

**pick_folder_dialog uses raw COM pointers and CP_ACP conversions; non-ANSI folder names are mangled and the dialog has no owner window**

Reporters: tools-ui-chunk3-L5006-7525

Evidence: `5339: IFileOpenDialog* dlg = nullptr; ... 5355 folder->Release(); 5375 item->Release(); 5378 dlg->Release();
5359: if (SUCCEEDED(dlg->Show(nullptr)))
5367-5370: int len = WideCharToMultiByte(CP_ACP, 0, wpath, -1, nullptr, 0, nullptr, nullptr); ... WideCharToMultiByte(CP_ACP, 0, wpath, -1, buf.data(), len, nullptr, nullptr);`

Why: A sweep or curated folder whose name has characters outside the system code page converts lossily to '?' and then fails std::filesystem::exists at 5320/5299, so Audition silently shows '(no .wav files)'. Show(nullptr) leaves the GLFW main window interactive behind a supposedly modal picker. The manual Release chain is correct today only because there are no early returns.

Recommendation: Wrap the COM interfaces in a small unique_ptr-with-Release deleter (or wil::com_ptr), pass glfwGetWin32Window(window) as the owner, and when the cross-platform pass comes, carry paths as std::filesystem::path built from the wide string instead of narrowing to ACP.


## F388 [CONFIRMED-BY-MATT'S-CLAUDE] critical (reporter: critical) rt-safety — tools/mforce_ui/main.cpp:5401

**audition_load_at resizes/refills the buffer g_bufferPlayback points into before taking g_audioMutex (audio-thread use-after-free)**

Reporters: arch-render-pipeline, tools-ui-chunk3-L5006-7525

Evidence: `5401: g_audition.currentBuffer.resize(size_t(frames));
5402-5403: for (...) g_audition.currentBuffer[size_t(i)] = interleaved[size_t(i) * 2];
5406-5408: { std::lock_guard<std::mutex> lock(g_audioMutex); g_bufferPlayback = g_audition.currentBuffer.data(); ...
3933 (callback, under lock taken at 3864): s += g_bufferPlayback[g_bufferPlaybackPos++];`

Why: While a WAV is playing, g_bufferPlayback already equals currentBuffer.data(). Repeat (5574: audition_load_at(g_audition.currentIdx)), Prev (5566), Next (5576) and a list click (5605) call audition_load_at without stopping first; the resize at 5401 can reallocate and free the storage the audio callback is indexing, and the fill loop writes into it concurrently even when capacity suffices. The identical hazard for g_outputWaveform was recognised and solved with buffer_playback_detach (4987-4993, 'detach it (under the audio lock) before that vector is resized or cleared'), but the audition path never adopted it.

Recommendation: At the top of audition_load_at call audition_stop() (or buffer_playback_detach()) before touching currentBuffer; or decode into a fresh local vector and std::swap it into g_audition.currentBuffer inside the same lock_guard that publishes g_bufferPlayback. Audition's save/cancel resume paths (5733, 5746) are already safe because they stop first.

Also reported as: audition_load_at resizes g_audition.currentBuffer before detaching g_bufferPlayback — audio thread can read freed memory


## F389 [UNVERIFIED] low (reporter: low) duplication — tools/mforce_ui/main.cpp:5941

**Cluster of small duplicated helpers and one dead wrapper in the shape/audition/curve sections**

Reporters: tools-ui-chunk3-L5006-7525

Evidence: `5941-5945 shape_ed_segment_values == shape_ed_array(node, "values") at 5970-5974;
5961-5964 shape_editor_apply_shaper is defined (and forward-declared at 5957) but never called;
5908-5937 shape_editor_open_curve/_segment/_shaper differ only in client and logX;
5300-5307 and 5321-5328 identical directory_iterator + lowercase-extension scan loops;
7085-7097 curve_exists_for vs 7512-7524 target_exists_for (same stash walk, one extra predicate);
5221-5256 spinner_int / spinner_float duplicate the width-sizing logic;
5479 and 5592 both do wav.substr(0, wav.size() - 4).`

Why: Each pair is small, but together they are ~90 lines of parallel code whose behaviour can drift (e.g. the two directory scans would need the same fix if extension matching changes), and the dead wrapper misleads a reader into thinking there is a plain-'values' apply path distinct from the named one.

Recommendation: Delete shape_editor_apply_shaper and shape_ed_segment_values (use shape_ed_array); fold the three open_* into shape_editor_open(node, Client, logX); add list_files_with_ext(folder, ext); make target_exists_for take a 'requireCurve' flag and drop curve_exists_for; template spinner<T>; strip_ext helper.


## F390 [UNVERIFIED] high (reporter: high) rt-safety — tools/mforce_ui/main.cpp:6093

**Live shape/knot data replaced on DSP objects without g_audioMutex while the node-graph stream may be pulling them on the audio thread**

Reporters: tools-ui-chunk3-L5006-7525

Evidence: `6094: cn->knots  = node.curveKnots;   (shape_editor_apply_curve, no lock)
7456: cn->knots = sortedKnots;        (draw_curve_node, no lock)
7327: cn->exprKnots = sorted;         (draw_curve_expr_rows, no lock)
6009 / 6030 / 6081: node.push_array(...) -> 508: dspSource->set_array(name, v) -> shaper_source.h:130 values_ = std::move(v); segment_source.h:120 values_ = std::move(v);
Contrast 6051: std::lock_guard<std::mutex> lock(g_audioMutex); node.dspSource->set_setting("timeMode", ...)`

Why: play_continuous captures node dspSources by raw pointer (4925: sc.source = sn->dspSource.get()) and the callback calls next() on them under the lock (3961). CurveNode::next() -> map() -> Curve::eval(knots) (curve_node.h 50-54, 87-93) and ShaperSource/SegmentSource read values_/segs_ in next(). A vector reassignment on the UI thread frees the old buffer mid-read on the audio thread (UAF / torn size-vs-data). The file already treats set_setting this way (6051, 9409, 9521 with the 9518 comment naming exactly this hazard), so these sites are an inconsistency, not a design choice. The same gap exists at the Properties-pane push_array sites 9826/9857/9934 (outside this unit).

Recommendation: Prepare the new vector outside the lock, then take g_audioMutex only for the swap: wrap the three knot assignments and GraphNode::push_array (line 504) in a lock_guard. Because the audio callback holds the mutex for a whole block, keep the critical section to the pointer/vector swap only.


## F391 [UNVERIFIED] high (reporter: high) god-class — tools/mforce_ui/main.cpp:6100

**draw_shape_editor is a ~930-line function with the Curve/Segment/Shaper client switch repeated more than a dozen times**

Reporters: tools-ui-chunk3-L5006-7525

Evidence: `6103-6104: const bool isSeg = ...; const bool isShaper = ...;
client branches at 6105-6108, 6123-6137, 6142, 6167, 6174/6185, 6362, 6419-6420, 6492, 6542/6580/6597, 6636, 6643, 6652, 6694, 6751, 6782, 6788-6796, 6808, 6869-6898, 6966-6974, 6992, 7014-7021;
smoothness pin lookup copy-pasted 3x: 6504-6508, 6553-6557, 6599-6603;
clamp rules duplicated at 6788-6796 and 6966-6974.`

Why: Load/apply/clamp/evaluate/axis-default rules for each client are scattered across the function instead of living in one adapter, so adding a fourth client (or changing Shaper's clamp range, currently a literal -2..2 at 6792 and 6970) means touching ~15 sites; morph mirroring (6810-6862) is interleaved with the generic insert path, which is why that block needed three bounds-guarded index dances. The function also mixes view math (tx/ty/fx/fy, grid, zoom/pan), rendering and interaction, so none of it is testable without ImGui.

Recommendation: Introduce a ShapeClient interface {load(node)->pts, apply(node, pts), clamp(x,y), eval(x), axisDefaults, supportsLogX, supportsSegs} with Curve/Segment/Shaper implementations; a CanvasView struct holding xMin..yMax + tx/ty/fx/fy + grid/zoom/pan; and split the body into draw_toolbar, draw_overlays, hit_test, apply_edits. The morph-twin mirroring becomes one helper taking (pts, segs, otherPts, otherSegs, k).


## F392 [UNVERIFIED] low (reporter: low) workaround-hack — tools/mforce_ui/main.cpp:6543

**Shaper overlay instantiates a ShaperSource per frame and calls next() purely for its side effect of loading smoothness**

Reporters: tools-ui-chunk3-L5006-7525

Evidence: `6543: ShaperSource probe;
6550-6551: probe.set_array("values", std::move(flat)); probe.set_array("segs", ShaperSource::encode_segs(segs));
6558-6559: probe.set_param("smoothness", std::make_shared<ConstantSource>(smooth));
           probe.next();  // loads smoothness into the interpolator`

Why: Relies on an undocumented ordering inside ShaperSource::next() to prime static evaluation; if the engine ever defers that load or makes next() advance state, the preview silently changes. Also two vector copies, a decode_segs and a heap-allocated ConstantSource per frame on the UI thread, whereas the morph overlay at 6681 already evaluates statically through Curve::eval_seg(..., smoothness).

Recommendation: Expose a static ShaperSource::eval(values, segs, smoothness, x) (or reuse Curve::eval with the shaper domain) and call it from the overlay; drop the probe object and the next() call.


## F393 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:7054

**curve_eval re-implements CurveNode::map (LogX) in the UI while the sibling editor already evaluates through the engine**

Reporters: tools-ui-chunk3-L5006-7525

Evidence: `7053-7054: // Mirror of CurveNode::map (LogX/LogLog) for the plot preview.
static float curve_eval(const nlohmann::json& curve, float freq) { ... lf = std::log(freq / hz(i - 1)) / std::log(hz(i) / hz(i - 1)); ...}
engine curve_node.h 87-93: float map(float x) const { ... return Curve::eval(knots, {}, d, 0.5f, x); }
7462-7467 (draw_curve_node): // Plot, evaluated through the ENGINE's own map() rather than a mirror of it ... CurveNode probe;`

Why: Two evaluators for the same breakpoint semantics; the mirror only does LogX (not LogLog despite its comment) and will silently diverge from Curve::eval_core's smoothness/domain handling the next time the engine changes. The file itself states the policy at 7462.

Recommendation: Delete curve_eval; in draw_one_curve build a CurveNode probe from the JSON rows (interp LogX) exactly as draw_curve_node does at 7465-7467, and share one plot helper (see the table/plot duplication finding).


## F394 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:7142

**Three copies of the breakpoint-table + log-spaced PlotLines editor idiom**

Reporters: tools-ui-chunk3-L5006-7525

Evidence: `draw_one_curve 7142-7211, draw_curve_expr_rows 7253-7350, draw_curve_node 7403-7478. Identical plot block in each:
7205: float f = lo * std::pow(hi / lo, float(i) / float(N - 1));
7345: samples[i] = probe.map(lo * std::pow(hi / lo, float(i) / float(N - 1)));
7474: samples[i] = probe.map(lo * std::pow(hi / lo, float(i) / float(N - 1)));
and the same row pattern (InputFloat x with floor clamp, IsItemDeactivatedAfterEdit->needSort, y, 'x' remove button gated on min count, '+ point' doubling last x, stable_sort, mark_graph_dirty).`

Why: The 2026-08-22 commit-time-sort fix had to be applied to each copy; the min-knot guards differ (2 vs >1) by copy rather than by intent; ~120 lines of near-identical widget code to keep in sync. The 7219-7223 comment calls the duplication deliberate, but the stated reason (keep the table idiom) is satisfied by a shared helper.

Recommendation: Extract plot_log_x(const char* id, float lo, float hi, const char* overlay, auto&& f) for the 128-sample PlotLines and a knot_table(rows, minRows, xFloor, onRemove) helper; have draw_one_curve delegate to the same helpers over a temporary CurveNode built from the stash rows.


## F395 [UNVERIFIED] medium (reporter: medium) efficiency — tools/mforce_ui/main.cpp:7228

**curve_node_destination is O(nodes x links x nodes x pins) and runs every frame to build a header string**

Reporters: tools-ui-chunk3-L5006-7525

Evidence: `7229: for (const auto& n : s_nodes) {
7233:     for (const auto& link : s_links) {
7234-7238:   const Pin* src = find_pin(link.startPinId); const Pin* dst = find_pin(link.endPinId); ... find_node_for_pin(link.startPinId); find_node_for_pin(link.endPinId);
7239:     if (srcNode && dstNode && srcNode->id == curve.id && dstNode->id == n.id)
(find_pin / find_node_for_pin at 806-820 scan every pin of every node)
7359-7360: snprintf(header, ..., curve_node_destination(node).c_str(), ...) called from draw_curve_node each frame (9301).`

Why: For a 60-node/80-link patch this is ~10^7 pin comparisons per frame whenever a CurveNode is selected in Properties, plus a JSON items() walk over dynamicPins per node. The outer node loop is unnecessary: the answer only depends on links whose start pin belongs to the curve node.

Recommendation: Iterate s_links once, test link.startPinId against the curve node's own output pin ids, resolve the destination node once with find_node_for_pin; or cache the destination string keyed on g_graphEditCounter.


## F396 [UNVERIFIED] medium (reporter: medium) efficiency — tools/mforce_ui/main.cpp:7552

**Mappings table rebuilds a perform-rooted ancestry walk per pin per frame using linear scans (O(nodes x pins x links x depth) with std::function recursion)**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `std::function<const GraphNode*(const GraphNode*, int, bool&)> root_perform = [&](...) { ... for (const auto& link : s_links) { const GraphNode* dst = find_node_for_pin(link.endPinId); ... const GraphNode* src = find_node_for_pin(link.startPinId); if (const GraphNode* r = root_perform(src, depth + 1, viaCurve)) return r; } ... };
7582: for (auto& c : s_nodes) if (c.label == ref) { srcNode = &c; break; }
7598-7602: for (const auto& pin : n.inputs) { const GraphNode* src = find_source_node(pin.id); ... root_perform(src, 0, viaCurve);`

Why: find_node_for_pin (814-820) and find_source_node (2559-2571) are linear over every node and pin; root_perform calls find_node_for_pin twice per link per recursion level, and it is invoked for every input pin of every node every frame the dialog is open. The comment defends the depth limit, but the cost is the per-level full-link scan, not the depth: a 100-node/200-link patch is on the order of 10^7-10^8 compares per frame. The dialog is modeless, so this runs continuously.

Recommendation: Build a reverse adjacency once per frame (or cache keyed on g_graphEditCounter): unordered_map<int inputPinId, const GraphNode* src> and unordered_map<const GraphNode*, small vector of upstream nodes>; root_perform then walks pointers. Replace the std::function with a plain recursive static function taking the index. The same index would serve draw_node's is_pin_connected calls (8944, linear over s_links per pin per frame).


## F397 [UNVERIFIED] low (reporter: low) soundness — tools/mforce_ui/main.cpp:7556

**root_perform's viaCurve flag is never cleared on a dead-end branch, so a direct perform link can be reported as 'curve'**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `if (n->typeName == "CurveNode") viaCurve = true;
for (const auto& link : s_links) { ... if (const GraphNode* r = root_perform(src, depth + 1, viaCurve)) return r; }`

Why: viaCurve is a by-reference accumulator. For a node fed by a CurveNode that is NOT perform-rooted and separately by a PerformNode directly, the first (failing) branch sets viaCurve = true and the second branch returns the PerformNode; the Shape column then says 'curve' for a direct binding.

Recommendation: Return the flag with the result (e.g. std::pair<const GraphNode*, bool>) or pass viaCurve by value down the recursion so only the successful path's value propagates.


## F398 [UNVERIFIED] medium (reporter: medium) workaround-hack — tools/mforce_ui/main.cpp:7796

**Mappings dialog carries two binding models at once: wires are declared canonical, but Add still creates legacy paramMap stash entries**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `7544: // A binding is now a wire, so this table is a VIEW
7625: ImGui::TextDisabled("Legacy paramMap entries (not convertible to nodes)");
7796-7799: if (ImGui::Button("Add (follows note)##mapbare")) { ... s_loadedParamMap["frequency"] = target; ...
7812-7813: if (ImGui::Button("Add with curve##mapcurve")) { curves_add_entry("frequency", target, opts[selTarget].current);`

Why: The dialog's own text says bindings are wires and labels the stash as legacy/not-convertible, yet its only creation path appends to that legacy stash, and every non-frequency row is flagged "(inert)" (7644-7648). New patches therefore accumulate data the UI itself calls legacy. Add-path UI state also lives in function statics (selNode/selTarget/lastEditorSel, 7747-7749) indexing a vector rebuilt every frame, so the selection silently retargets when nodes are added or removed.

Recommendation: Make Add create the canonical form (a PerformNode output -> optional CurveNode -> target pin wire, the same objects the top table already reads) and reduce the stash section to a delete-only migration view. Move the add-form selection to a small struct keyed by node id rather than vector index.


## F399 [UNVERIFIED] high (reporter: high) soundness — tools/mforce_ui/main.cpp:7858

**QWERTY key-up handling is inside the key-down gate, so a held live note can be stranded (stuck voice)**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `if (g_transport.noteMode && !kbDisabled && !ImGui::GetIO().WantTextInput &&
    !ImGui::GetIO().KeyCtrl) { ... if (ImGui::IsKeyReleased(s_qwertyMap[i].key) && s_qwertyHeldNote[i] >= 0) { release_note_held(s_qwertyHeldNote[i]); s_qwertyHeldNote[i] = -1; } }`

Why: In Live mode a voice sustains until release_note_held fires gate_release (Voice.held comment 3711-3713, release_note_held 4829). If the key is released while Ctrl is down, while a text field has focus, or after PC Keyboard/graph mode flips kbDisabled, the release branch never runs: s_qwertyHeldNote[i] stays set, the voice keeps sounding, is_playing() stays true (Play button shows Stop), and the next press of that key only re-arms it. The mouse path explicitly avoids this (8025-8031: release on mouse-up anywhere); the QWERTY path does not. Audible stuck notes are the kind of defect that embarrasses a release.

Recommendation: Hoist the release loop out of the gate: for every i with s_qwertyHeldNote[i] >= 0, call release_note_held on IsKeyReleased regardless of noteMode/WantTextInput/KeyCtrl; additionally release all held QWERTY notes when noteMode turns off or kbDisabled becomes true. While there, replace the lazy-init pair s_qwertyHeldNote/s_qwertyHeldInit (7864-7869) with a file-scope std::array<int, QWERTY_MAP_COUNT> initialized to -1 via an immediately-invoked lambda or fill.


## F400 [UNVERIFIED] low (reporter: low) rt-safety — tools/mforce_ui/main.cpp:7941

**Keyboard highlight reads Voice.active / midiNote from the UI thread without the audio mutex (formal data race)**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `auto isNoteActive = [](int midi) {
    for (int vi = 0; vi < MAX_VOICES; ++vi)
        if (g_voices[vi].active && g_voices[vi].midiNote == midi) return true;`

Why: Voice::active and midiNote are plain bool/int (3717-3718) written by the audio callback under g_audioMutex (3864) and here read with no synchronization; any_voice_active (3842-3846) has the same pattern, so this follows precedent, but it is undefined behaviour by the standard and only benign because of x86/MSVC's strong memory model. Not an audio-thread cost; the fix must not add a lock to the callback.

Recommendation: Have the callback publish what the UI needs atomically, as it already does for g_audioActiveVoices (3876): e.g. a std::atomic<uint32_t> active-voice bitmask plus std::atomic<int> midiNote per voice with relaxed ordering; the UI reads those.


## F401 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:7955

**On-screen keyboard still assumes the single-zone QWERTY map: lower-zone keys (offsets -12..-1) play below the drawn range and get no label or highlight**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `int baseNote = g_transport.octave * 12;
...
int chromOffset = oct * 12 + whiteOffsets[w];
if (chromOffset < 20) {
    const char* ql = qwerty_label_for_offset(chromOffset);`

Why: s_qwertyMap (5162-5202) now has a lower zone at offsets -12..-1 ("1 octave down", Matt 2026-09-13) and an upper zone 0..19. The keyboard draws from baseNote upward and only looks up labels for 0..19, so the twelve Z/S/X/D/C/V/G/B/H/N/J/M keys fire notes that are never drawn, never labelled, and never highlighted by isNoteActive. The literal 20 is a stale coupling to the old map extent.

Recommendation: Derive the label range from the map (min/max of s_qwertyMap[].offset, computed once as constexpr or a static) and start the drawn keyboard at baseNote + minOffset (one octave below) when noteMode is on, so both zones are visible and highlightable.


## F402 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:8037

**Key rectangle geometry is computed twice: once in the draw loops and again, verbatim, in the hit-test loops**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `8039-8043: int whiteIdx = oct * WHITE_KEYS_PER_OCT + blackKeys[b].afterWhite; float x0 = origin.x + (whiteIdx + 1) * keyW - blackW * 0.5f; ... (identical to 7994-7998)
8056-8061: int oct = idx / WHITE_KEYS_PER_OCT; int w = idx % WHITE_KEYS_PER_OCT; float x0 = origin.x + idx * keyW; ... float x1 = x0 + keyW - 1.0f; (identical to 7959-7964)`

Why: Four copies of the same rect math (white draw 7958-7964, black draw 7992-7998, black hit 8037-8043, white hit 8055-8061) is the main reason draw_keyboard_panel is 260 lines, and any geometry tweak (the 1 px gap, blackW ratio) must be made in two places or hit-testing silently diverges from what is drawn.

Recommendation: One pass builds a small fixed-capacity array of {ImRect, midiNote, isBlack} (max 10 octaves -> 121 entries, stack or static), then draw iterates it and the click handler iterates it in reverse (black first) with ImGui::IsMouseHoveringRect / rect.Contains(mousePos). Velocity-from-y and the Live/fixed dispatch (8072-8082) then exist once.


## F403 [UNVERIFIED] high (reporter: high) duplication — tools/mforce_ui/main.cpp:8098

**Chords and Drums keep a second render pipeline beside generate_unified, duplicating post-processing and skipping the spectrum/evo updates**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `8093: // (render_passage_waveforms deleted 2026-09-13: generate_unified renders passages through the engine ...)
8139-8148: float peak = 0.0f; ... if (peak > 0.95f) { float scale = 0.95f / peak; ...
8265-8273: float peak = 0.0f; ... if (peak > 1.0f) { float scale = 0.95f / peak; ...
8159 / 8277: for (auto& node : s_nodes) node.waveformData.clear();`

Why: generate_unified (4347-4470) is the engine-authoritative render: it fills g_outputWaveform, captures per-node strips, rebuilds evo snapshots and calls compute_output_spectrum (the only call site, 4462). render_chords_waveforms and render_drums_waveforms re-implement the UI publish step by hand with inconsistent rules (peak-normalize threshold 0.95 vs 1.0; generate_unified does not normalize at all), clear every node strip instead of capturing, never call compute_output_spectrum or clear g_evoSnapshots (so after a Chords/Drums Generate the Spectrum window and scrubber still show the previous Note/Passage render), and chords adds a nearest-neighbour resample hack (8164-8171) for a sample-rate mismatch generate_unified simply ignores. Three ways to get a buffer onto the screen is exactly the second-renderer debt the 2026-09-13 deletion note says was retired.

Recommendation: Route Chords through generate_unified by turning the Conductor's performed notes into std::vector<SchedNote> (the Part/Conductor already produces timed notes); for Drums either give generate_unified a kit variant or, at minimum, extract one publish_render(std::vector<float>&& mono, int sampleRate) helper that performs detach -> assign -> g_waveformSamples -> strip clear -> wave_view_after_render -> compute_output_spectrum -> g_evoSnapshots.clear() so every pipeline ends identically. Decide once whether normalization belongs in the UI (it does not for Note/Passage today) and apply it uniformly.


## F404 [UNVERIFIED] medium (reporter: medium) rt-safety — tools/mforce_ui/main.cpp:8275

**render_drums_waveforms resizes g_outputWaveform without buffer_playback_detach(), violating the documented invariant**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `g_outputWaveform.assign(mono.begin(), mono.end());
g_waveformSamples = frames;`

Why: 4987-4988 states: g_bufferPlayback points INTO g_outputWaveform, detach it (under the audio lock) before that vector is resized or cleared. generate_unified (4414) and render_chords_waveforms (8156) do; the drums path does not. It is currently masked because every caller either detaches first (Generate state machine, 13596) or runs only when !is_playing() (8754, 13771, 13831), but the function itself hands the audio thread a dangling pointer if any future caller (MIDI-triggered generate, a headless/scripted path) invokes it during playback.

Recommendation: Add buffer_playback_detach() immediately before the assign at 8275 (or fold into the shared publish_render helper recommended above so the invariant lives in one place).


## F405 [UNVERIFIED] medium (reporter: medium) rt-safety — tools/mforce_ui/main.cpp:8275

**g_bufferPlayback raw-pointer-into-vector invariant is enforced by convention at four sites and the drums path omits the required detach**

Reporters: arch-ui-tools

Evidence: ``static const float* g_bufferPlayback = nullptr;` (3697) points into g_outputWaveform; 1106-1110: 'any resize/clear of that vector must call buffer_playback_detach() first (backlog 3k: a reallocation mid-playback was an audio-thread read of freed memory)'. Detach present at 4414-4415, 8156-8157, 13596-13597; absent at render_drums_waveforms `g_outputWaveform.assign(mono.begin(), mono.end());` (8275). Reachability: every transport_play entry is gated on !is_playing() (8753-8757, 13771-13773, 13830-13835) and Generate goes through the s_genState detach (13594-13600), so the drums miss is latent today, not live.`

Why: The invariant is one unguarded entry point away from the exact audio-thread use-after-free backlog 3k already recorded; the fix was applied per-site rather than structurally.

Recommendation: Have the audio engine own the playback buffer as shared_ptr<const std::vector<float>> swapped through the command queue; the display buffer then has no aliasing relationship with the callback.


## F406 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:8420

**transport_generate overwrites the renderers' error status with 'Generated N ...', and transport_play plays after a failed regenerate**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `render_chords_waveforms(chords, g_transport.bpm, fig, spreadMs);
char buf[128];
snprintf(buf, sizeof(buf), "Generated %d chords", (int)chords.size());
transport_set_status(buf, false);`

Why: render_chords_waveforms returns void and can bail with transport_set_status("No patch file loaded ...", true) (8101-8104) or swallow an exception to stderr (8174-8176); the caller then reports success anyway. Same shape for Drums: errors set at 8206-8209, 8229-8230, 8240-8243, 8280-8282 are clobbered by 8437-8440. In transport_play's Note case (8471-8473) `if (stale) transport_generate(); play_buffer();` replays whatever buffer exists even when generate_unified returned false, because transport_generate is void. The user is told a render succeeded when it did not.

Recommendation: Make render_chords_waveforms / render_drums_waveforms return bool (mirroring generate_unified), have transport_generate return bool and set the success status only on true, and in transport_play skip play_buffer when the regenerate failed.


## F407 [UNVERIFIED] low (reporter: low) duplication — tools/mforce_ui/main.cpp:8500

**save_wav_dialog is a copy of text_save_dialog**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `char filename[MAX_PATH] = "output.wav"; OPENFILENAMEA ofn{}; ofn.lStructSize = sizeof(ofn); ofn.lpstrFilter = "WAV Files\0*.wav\0All Files\0*.*\0"; ... ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR; ofn.lpstrDefExt = "wav"; if (GetSaveFileNameA(&ofn)) { remember_feature_dir("wav", filename); return filename; }`

Why: Lines 8500-8516 duplicate text_save_dialog at 1294-1311 field for field; the only difference is the "renders" fallback when no directory is remembered. Two copies of Win32 OPENFILENAME setup is one more place to miss when the dialog code is eventually made cross-platform.

Recommendation: Replace with `return text_save_dialog("wav", "WAV Files\0*.wav\0All Files\0*.*\0", "wav", "output.wav");` after giving text_save_dialog an optional default-directory argument (or letting feature_initial_dir take a fallback).


## F408 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:8550

**Dead code: transport_label, transport_label_inline, and the unused figurePrefix parameter**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `static void transport_label(const char* label, float labelW) { ... }
static void transport_label_inline(const char* label) { ... }
8099: const std::string& figurePrefix, float spreadMs) {   // figurePrefix never referenced in the body`

Why: A grep shows transport_label and transport_label_inline are defined and never called (only transport_label_snug is used); render_chords_waveforms takes figurePrefix (passed `fig` at 8420) and never uses it, so the Figure field's effect on chords is only via parse_chord_string, which the signature obscures. `float lw = 70.0f; // label width` at 8604 is likewise unused.

Recommendation: Delete the two helpers, the lw local, and the figurePrefix parameter (and its argument at 8420); MSVC /W4 with -Wunused-parameter equivalents would flag the parameter.


## F409 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:8550

**Dead UI helpers and leftover statements**

Reporters: arch-duplication

Evidence: `transport_label 8550 and transport_label_inline 8555 defined, never called (only transport_label_snug is used); `float lw = 70.0f; // label width` 8604 unused; voice_schedule 3812-3820 wrapper never called (all paths use voice_schedule_unlocked); apply_score_defaults 5140-5145 assigns `g_transport.velocity = vel; g_transport.duration = dur;` then again with a clamp and a stray `(void)0;`; `(void)freqDefault;` 10705 after computing it; GraphNode::performField 324 kept with a "Legacy" comment; basic_additive_source.h:97-111 and pulse_source.h:54-59 call `->next()` then overwrite the result with `->current()`; hybrid_ks_source.cpp:108-109 `tblPeak` and 115-117 `maxA` computed and never used.`

Why: Small, but each is a reader trap ("is this the real path?") in a 14.6k-line file.

Recommendation: Delete the unused helpers/locals; keep one velocity/duration assignment.


## F410 [UNVERIFIED] low (reporter: low) duplication — tools/mforce_ui/main.cpp:8623

**Duplicated literal tables inside draw_transport_panel: note names (x2) and the two-line text-box height (x2)**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `8623: static const char* kPitch[12] = {"C","C#","D","D#","E","F", ...
8834: static const char* kNames[12] = {"C","C#","D","D#","E","F", ...
8656-8657 and 8688-8689: const float twoLine = ImGui::GetTextLineHeight() * 2.0f + ImGui::GetStyle().FramePadding.y * 2.0f + 6.0f;`

Why: Two identical pitch-class tables live 200 lines apart in the same function (one in a case block, one at the bottom), and the multiline-field height formula is pasted into both the Passage and Chords cases. Low cost today, but the engine's music layer is where a note-name table belongs (durn_converter carries a third copy at tools/durn_converter/main.cpp:456).

Recommendation: One file-scope `static constexpr const char* kNoteNames[12]` (or expose one from engine/include/mforce/music) and a `static float two_line_box_height()` helper.


## F411 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:8625

**Octave has two sources of truth (noteStr vs g_transport.octave) that only sync in one direction**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `int noteNum = int(parse_note_input(g_transport.noteStr));
int pc = ((noteNum % 12) + 12) % 12;
int oct = std::clamp(noteNum / 12, 0, 8);
...
if (nchg) { snprintf(g_transport.noteStr, ..., "%s%d", kPitch[pc], oct); g_transport.octave = oct; }`

Why: The Note tab derives its Octave spinner from noteStr and pushes to g_transport.octave only on change; the Passage tab (8662), Chords tab (8709) and the keyboard arrow keys (7894-7897) edit g_transport.octave directly and never touch noteStr. After pressing Up on the keyboard or changing octave on another tab, the Note tab shows one octave while QWERTY/on-screen keys play another, and the next Note-tab edit snaps g_transport.octave back.

Recommendation: Pick one storage. Simplest: g_transport.octave and a pitch-class int are the state; noteStr is composed from them whenever parse_note_input needs it (or keep noteStr as storage and make every other octave writer go through one set_octave() that rewrites noteStr).


## F412 [UNVERIFIED] medium (reporter: medium) smell — tools/mforce_ui/main.cpp:8712

**Inversion and Spread spinners on the Chords tab write fields nothing reads**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `transport_label_snug("Inversion");
spinner_int("inv", &g_transport.inversion, 1, 0, 4);
transport_label_snug("Spread");
spinner_int("sprd", &g_transport.spread, 1, 0, 4);`

Why: A grep for g_transport.inversion / .spread hits only these two lines; parse_chord_string and render_chords_waveforms (8414-8420) never receive them. The UI presents live controls that have no effect, which is the kind of lie a user discovers by ear and then distrusts every other knob.

Recommendation: Either plumb them into the chord path (parse_chord_string / ChordPerformer) or delete the two spinners and the TransportState fields (3619-3620); g_transport.repeats (3625) is likewise never read.


## F413 [UNVERIFIED] medium (reporter: medium) smell — tools/mforce_ui/main.cpp:8920

**No undo/redo and no edit abstraction: dirty flag and instrument-cache invalidation are maintained by convention at ~65 call sites, with misses**

Reporters: arch-ui-tools

Evidence: `mark_graph_dirty (791-794) is the only edit primitive; grep shows ~65 call sites in widget callbacks that mutate s_nodes/s_links/dspSource directly. Miss: draw_node mixer '+' button `if (ImGui::SmallButton(btnLabel)) node.add_channel_input();` (8920-8921) adds a pin without mark_graph_dirty or invalidate_instrument_cache. ui_plan.md:165: 'Undo/Redo — not yet, but the architecture supports it (command pattern on graph edits)' — no command type exists anywhere in the file.`

Why: A missed mark means the Save-on-close prompt is skipped and, because get_cached_instrument keys on g_graphEditCounter (4630-4632), the live keyboard plays a stale instrument after the edit. Undo cannot be added without first funnelling edits through one place.

Recommendation: Route every mutation through GraphDocument::apply(Edit) which marks dirty, bumps the edit counter, and pushes the inverse onto an undo stack; widgets only construct Edit values.


## F414 [UNVERIFIED] low (reporter: low) modern-cpp — tools/mforce_ui/main.cpp:8947

**Per-pin per-frame std::string allocation for a prefix test**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `} else if (pin.inputOnly || pin.name.substr(0, 3) == "ch ") {`

Why: substr materialises a temporary std::string for every unconnected input pin every frame; C++20 has starts_with. Minor, but it is in the hottest UI loop (every node, every pin) and the fix is a one-token change.

Recommendation: pin.name.starts_with("ch "). The same applies to val.value("ref", std::string("?")) at 8968, which constructs two strings per dynamic pin per frame; read the ref once when the pin is promoted and cache it on the Pin.


## F415 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:9008

**Node face width 150.0f and the right-align formula are hardcoded in three places**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `9008: ImGui::Indent(150.0f - textWidth - 20);
9016-9018: float nodeWidth = 150.0f; float textWidth = ...; ImGui::Indent(nodeWidth - textWidth - 20);
9101: ImGui::Indent(150.0f - textWidth - 20);`

Why: The same magic width and padding (20) is repeated for tap pins, regular output pins and group output pins; changing node width means editing three sites and the strips/other panels that assume it.

Recommendation: constexpr float kNodeFaceWidth = 150.0f; constexpr float kPinLabelPad = 20.0f; and a static right_aligned_pin_label(const char*) helper used by all three.


## F416 [UNVERIFIED] low (reporter: low) efficiency — tools/mforce_ui/main.cpp:9055

**draw_group_node computes the group boundary twice per group per frame (group_output_node repeats group_boundary and may topo_sort)**

Reporters: tools-ui-chunk4-L7526-9195

Evidence: `group_boundary(g, ins, outs);
GraphNode* outNode = group_output_node(g);`

Why: group_output_node (2682-2694) calls group_boundary again on the same group and, when there is no explicit boundary output, runs collect_member_labels + topo_sort(). group_boundary itself is O(links x node-lookups). The `outs` vector just computed at 9055 is discarded. Also pin ids are minted inside the draw function (9058 `g.inPinIds.push_back(next_id())`), mixing model mutation into rendering.

Recommendation: Give group_output_node an overload taking the already-computed `outs` (or return {ins, outs, outNode} from one call), and mint synthetic pin ids where the group is created/edited rather than during draw.


## F417 [UNVERIFIED] high (reporter: high) god-class — tools/mforce_ui/main.cpp:9227

**draw_properties_panel is a 1,035-line function dispatching on typeName strings with three nested lambdas and function-local statics**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `9227 `static void draw_properties_panel() {` ... 10261 `}`; per-type branches: 9296 `if (node->typeName == "CurveNode")`, 9632 `== "SegmentSource"`, 9694 `== "Shaper"`, 9753 `== "ExplicitPartials"`, 9764 `== "SegmentSource"` again, 9944 `== "FormantSpectrum"`, 10031 `== NT_ENVELOPE`, 10193 `== NT_PATCH_OUTPUT`, 10248 `== NT_PARAMETER || == "NameGate"`; lambdas apply_setting_value 9406, render_setting_row 9418 (113 lines); statics 9241-9243 and 9275-9277 for rename buffers.`

Why: Every node type's property UI lives in one scope; adding a type or changing the settings layout means editing inside a kilo-line function whose locals (labelW, widgetW, hasParams, isEnvNode) are shared across unrelated sections. The length is also why the lock discipline (previous finding) and the duplicated edit blocks (next finding) went unnoticed.

Recommendation: Split into draw_group_properties, draw_node_header_rename, draw_pin_rows, draw_settings (with render_setting_row as a static function), draw_array_tables, draw_segment_preview, draw_shaper_preview, draw_formant_table, draw_envelope_stages, draw_output_props; dispatch per-type sections through a small table keyed by typeName. Keep the rename edit state in a tiny struct instead of function-local statics.


## F418 [UNVERIFIED] low (reporter: low) modern-cpp — tools/mforce_ui/main.cpp:9316

**substr-based prefix/suffix tests allocate per pin per frame where C++20 starts_with/ends_with are free**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `9316 `if (pin.name.substr(0, 3) == "ch ") continue;` (per pin, per frame); 10303 `if (p.name.substr(0, 3) == "ch ")`; 9566 `n.size() <= 5 || n.substr(n.size() - 5) != "Curve"` re-implements the `is_env_shape` lambda at 9402 and `ends_with`.`

Why: Each substr constructs a std::string; in the Properties loop it runs for every input pin every frame. Trivial per call but pure waste, and the suffix test is a second copy of logic already present ten lines up.

Recommendation: `pin.name.starts_with("ch ")`, `n.ends_with("Curve")`, and reuse is_env_shape at 9566.


## F419 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:9337

**Repeated blocks inside draw_properties_panel: pin-edit apply, setting apply, centered table header, preview plotting**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `(a) 9337-9348 and 9370-9380 are the same `PushItemWidth; snprintf("##prop.."); step = max(0.001f, |defaultValue|*0.01f); InputFloat; constantSrc->set; jsonExtras.erase; update_node_dsp; mark_graph_dirty; PopItemWidth` block; (b) 9516-9530 `if (changed && node->dspSource) { lock; set_setting; jsonExtras.erase; for arrayValues v = get_array; mark_graph_dirty; }` re-implements the `apply_setting_value` lambda at 9406-9416 fifteen lines above (which already does lock/set_setting/erase/get_array/dirty); (c) centered header rows 9961-9972 and 10061-10074 are verbatim copies; (d) three N=256 sample→min/max→pad→PlotLines previews at 9651-9681, 9710-9718, 10130-10187 with 9670-9676 and 10176-10182 identical.`

Why: The duplicated apply blocks are where the lock discipline diverged (9521 locks, 9343/9375 do not); the preview trio is the kind of copy that drifts (segment and envelope use vmin/vmax padding, shaper uses fixed -1.6..1.6).

Recommendation: Extract `edit_pin_constant(GraphNode&, Pin&, float widgetW)`, make render_setting_row call apply_setting_value, add `table_centered_header(const char* const*, int)` and `plot_preview(const char* id, float* v, int n, float h)`.


## F420 [UNVERIFIED] low (reporter: low) efficiency — tools/mforce_ui/main.cpp:9699

**Per-frame heap work in previews and mapping_badge while a node is selected**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `9699-9709 `ShaperSource probe; probe.set_array("values", *vals); ... probe.set_param("smoothness", std::make_shared<ConstantSource>(smooth)); probe.next();` every frame the Shaper is selected; 10136 `std::vector<float> widths(nStages, 0.0f);` every frame; 9205-9225 mapping_badge builds `std::string target = nodeLabel + "." + name` and walks all of s_loadedParamMap, called per pin (9328) and per setting (9480).`

Why: UI thread only, so not an RT violation, but it is O(pins × paramMap) string work plus a full DSP object construction per frame for a static picture that only changes on edit.

Recommendation: Cache the preview samples and badge results keyed on g_graphEditCounter (already maintained for the instrument cache) and recompute on change.


## F421 [UNVERIFIED] high (reporter: high) rt-safety — tools/mforce_ui/main.cpp:9826

**Array/formant/constant edits mutate DSP objects the audio thread is streaming without the audio lock**

Reporters: arch-ui-tools

Evidence: ``if (changed) { node->push_array(d.name); mark_graph_dirty(); }` (9826, 9857, 9934; also 6009, 6030, 6081) -> push_array -> `dspSource->set_array(name, v)` (508) -> partials.h 1336-1343 `mult1Stat_ = std::move(v); ... init_arrays();`. `if (changed) { node->rebuild_formant_spectrum(); ... }` (10014) -> `spec->formants.clear()` (518). `pin.constantSrc->set(pin.defaultValue)` (9343, 9375) is a plain store (dsp_value_source.h:135 `void set(float v) { v_ = v; }`). Meanwhile play_continuous installs `g_streamSource = src;` (4883) pointing at these same objects, pulled per sample at 3940. Contrast 9517-9523, where set_setting IS locked with the comment 'set_setting can rebuild internal arrays (ExplicitPartials) while a stream tap is mid-next() on the same object (3k audit residual)'.`

Why: Replacing a std::vector or shared_ptr vector while the audio thread iterates it is a use-after-free on the audio thread (crash or garbage audio) during Stream/Listen; the unlocked float store is a formal data race. The 3k audit fixed set_setting but left set_array, rebuild_formant_spectrum and constantSrc->set with identical exposure.

Recommendation: Short term: route push_array / rebuild_formant_spectrum / constantSrc->set through the same guard as set_setting. Real fix: stop streaming the UI preview graph (see dual-representation finding) and make parameter publication an atomic slot or queued command on the engine-owned instrument.


## F422 [UNVERIFIED] high (reporter: high) rt-safety — tools/mforce_ui/main.cpp:10029

**Properties panel mutates live DSP objects (Envelope stage vector, arrays, formant list) without g_audioMutex while the continuous streams read them on the audio thread**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `10082-10109 DragFloat directly on `env->stage(i)` fields (`&s.percent`, `&s.ramp.startVal`, `s.ramp.type = RampType(curType)`, `&s.minSec`); 10036 `env->absolute_time = absTime;`; 10120 `env->remove_stage(removeIdx)`; 10121 `env->add_stage_default()` — stages_ is `std::vector<Stage>` (envelope.h:536). Also unlocked: 9826/9857/9934 `node->push_array(d.name)` → set_array (main.cpp 504-512), 10014 `node->rebuild_formant_spectrum()` which does `spec->formants.clear(); ... push_back` (515-526), 9446 `node->apply_config()`. The streams point at these objects: 4883 `g_streamSource = src;` and 4925 `sc.source = sn->dspSource.get();`, and 1103-1105 states the streams "hold raw pointers into node dspSources". Contrast 9517-9522 in the same function: "Under the audio lock: set_setting can rebuild internal arrays ... while a stream tap is mid-next() on the same object (3k audit residual)" `std::lock_guard<std::mutex> lock(g_audioMutex); node->dspSource->set_setting(...)`.`

Why: remove_stage/add_stage_default can reallocate or shrink stages_ while audio_callback is iterating it through g_streamSource — a read of freed memory on the audio thread (the same class of bug as backlog 3k). set_array/formants rebuild have the same shape. The panel already knows the rule for set_setting; the other mutation paths were added without it.

Recommendation: Route every DSP mutation from the panel through one helper that takes g_audioMutex (e.g. `with_audio_lock([&]{ ... })`), and for structural changes (add/remove stage, rebuild formants, set_array with a size change) call stop_streams() first as delete_node does (1115). Make GraphNode::push_array/apply_config/rebuild_formant_spectrum lock internally so no caller can forget.


## F423 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:10279

**find_node_by_id exists but the range re-rolls the id loop seven times and queries the imnodes selection three times in one menu**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `10279 `static GraphNode* find_node_by_id(int nodeId)`; inline copies at 10706-10712 `for (auto& n : s_nodes) { if (n.id == freqNodeId ...`, 11158 `for (auto& n : s_nodes) if (n.id == nodeId) { oldN = &n; break; }`, 11169-11172, 11240, 11282, 11482, 11614-11615; `ImNodes::NumSelectedNodes()` + GetSelectedNodes repeated at 11539, 11585, 11606-11608 in show_node_context_menu; `find_selected_node` (1097) is the same loop keyed on g_selectedNodeId.`

Why: Eight hand-written lookups for one query hide the one place a node-id index could live, and the repeated selection reads make the menu's targets/labels depend on call order.

Recommendation: Use find_node_by_id everywhere (declare it with the other lookup helpers near 814) and read the selection once at the top of show_node_context_menu into a local vector.


## F424 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:10688

**Dead leftovers in convert_node_to_patch_graph and clipboard_copy**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `10689 `float freqDefault = 440.0f;` ... 10697 `freqDefault = p.defaultValue;` ... 10705 `(void)freqDefault;`; freqPinId (10688) is only ever tested `>= 0`; 10445-10446 `Pin* outPin = find_pin(outPinId); Pin* inPin = find_pin(inPinId);` re-looks up pins that `a`/`b` (10434-10435) already hold.`

Why: A silenced unused variable is a half-finished thought; the redundant find_pin calls are two extra linear scans per link per copy.

Recommendation: Drop freqDefault and freqPinId (keep freqNodeId as the flag), and pick out/in from a/b by kind.


## F425 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:10742

**s_menuSourceAction is a global std::function side-channel set by whichever popup draws; menu_source throws if a caller forgets**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `10742 `static std::function<void(const char*)> s_menuSourceAction;` 10764 `s_menuSourceAction(typeName);` with setters at 10946 `s_menuSourceAction = create_source_at_menu;` and 11552 `s_menuSourceAction = [targetId](const char* t) { replace_node_with(targetId, t); };`.`

Why: The menu tree's behaviour depends on a global assigned earlier in the same frame; a third caller of source_family_menus that forgets the assignment gets std::bad_function_call at the first click. The comment at 10741 ('Set by whichever popup is drawing the tree, every frame it draws') documents the invariant instead of removing it.

Recommendation: Pass the action as a parameter: `source_family_menus(const std::function<void(const char*)>& on_pick)` (or a function_ref), and drop the global.


## F426 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:10787

**source_family_menus/experimental_family_menu are a hand-maintained second registry of ~75 (label, type) pairs**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `10790 `menu_source("Sine", "SineSource");` ... 10930 `menu_source("Bit Rotate (glitch)", "BitRotateEvolution");` — every registered type is listed by hand with a hand-written label; SourceRegistry already has `SourceCategory get_category(const std::string&)` and 'all registered types, sorted by category then name' (source_registry.h:43-49); `node_display_name(typeName)` (main.cpp:137, used at 9271) is a third naming table.`

Why: A type registered in the engine but not added here is unreachable from the create menu and from Replace-with; a label changed in one table but not the other shows two names for one node. Explicit registries are the project rule — this is a manual duplicate of the registry rather than a use of it.

Recommendation: Carry the menu label and an optional ordering/experimental flag on the registration (one place), and generate the family menus from SourceRegistry::get_category + that metadata; keep menu_sep groupings via an explicit sort key. At minimum add a startup check that every registered type appears in the menu table.


## F427 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:10974

**show_create_menu creates Output in two places with different side effects; special-node items skip the group/dirty handling the generic path does**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `10978-10981 `if (ImGui::MenuItem("Output")) { s_nodes.emplace_back(NT_PATCH_OUTPUT); SetNodeScreenSpacePos(...); }` (no mark_graph_dirty) vs 11012-11016 same item inside the Output submenu with `mark_graph_dirty();`; 10989-10992 Parameter and 11024-11034 Channel/Mixer also skip the drilled-group membership (`g->members.push_back(n.label)`) and update_node_dsp that create_source_at_menu does at 10753-10756; 10957-10965 Note re-implements create_source_at_menu's body inline.`

Why: Creating Output from the top-level item leaves the document clean (no save prompt on close); a Channel created while drilled into a group lands outside it. Two menu entries for one action is also a UX inconsistency.

Recommendation: Route every creation through create_source_at_menu (or a `create_node_at_menu(type, paramName)` that handles the special types), and delete the duplicate top-level Output item.


## F428 [CONFIRMED-BY-MATT'S-CLAUDE] critical (reporter: critical) soundness — tools/mforce_ui/main.cpp:11258

**Two live clipboard implementations are both bound to Ctrl+C/Ctrl+V, so one Ctrl+V pastes twice**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `10264: "Node clipboard — Edit > Cut/Copy/Paste, Ctrl+X/C/V" (clipboard_copy/clipboard_paste/clipboard_cut, s_clipboard json) vs 11259: "Copy/paste (Matt 2026-09-07). The clipboard carries a JSON fragment ... rides the OS clipboard" (copy_selection_to_clipboard/paste_fragment_text/paste_clipboard). Traced wiring in main(): 14255-14256 `if (editorHovered && KeyCtrl ...) { IsKeyPressed(ImGuiKey_C) copy_selection_to_clipboard(); IsKeyPressed(ImGuiKey_V) paste_clipboard(); }` and 14462-14464 `if (editorFocused && ... KeyCtrl) { X clipboard_cut(); C clipboard_copy(); V clipboard_paste(); }` — both blocks run in the same frame; Edit menu 13738-13743 calls the in-process set, create menu 10948 and node menu 11542 call the OS set.`

Why: With the editor hovered and focused (the normal state after clicking in it), Ctrl+C fills both clipboards and the next Ctrl+V runs paste_clipboard() (full save/merge/reload of the graph) AND clipboard_paste() (fresh nodes from the in-process JSON), producing two copies of the selection. The two paths also differ in coverage (in-process carries Output/Parameter/Mixer/Channel but not dynamicPins/curveKnots/jsonExtras/group membership; the fragment path is the inverse), so Edit-menu paste and Ctrl+V paste give different results. This is a user-visible correctness defect and ~500 lines of parallel code.

Recommendation: Keep one clipboard. The fragment format (saved form, OS clipboard, cross-instance) is the better contract; drop clipboard_copy/clipboard_paste/clipboard_cut and the 14462-14464 binding, bind Cut to the fragment path, and update the Edit menu to call copy_selection_to_clipboard/paste_clipboard. Then fix the fragment path's file round-trip (see the workaround finding).


## F429 [UNVERIFIED] medium (reporter: medium) workaround-hack — tools/mforce_ui/main.cpp:11269

**OS-clipboard copy/paste round-trips the whole graph through temp files and a full reload**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `11270 `return (std::filesystem::temp_directory_path() / name).string();`; copy 11296-11300 `save_patch_graph(tmp)` ... `std::ifstream f(tmp); f >> doc;` just to pick out `labels.count(jn["id"])` at 11310, although `serialize_patch_graph` (2806) returns the json in memory and save_patch_graph (3268-3273) is a four-line wrapper over it; paste 11349-11357 stages again, 11442 `{ std::ofstream f(merged); f << doc.dump(2); }` unchecked, 11444 `load_graph_from_path(merged);` rebuilds every node, then 11449/11455-11457 hand-restore s_currentFilePath and s_groupPath.`

Why: A paste of two nodes writes two files to %TEMP%, destroys and recreates every DSP object in the graph (stopping live audio, resetting selection, listen tap, camera and any state load_graph_from_path clears but the paste does not restore), and depends on filesystem success for an in-memory operation (temp_directory_path can throw; neither ofstream is checked). The stated rationale (fragment format cannot drift from file format) is achievable without the disk and without the reload.

Recommendation: Add `serialize_node_graph()` beside serialize_patch_graph so both savers are pure, build the fragment from the in-memory json, and add a loader entry point that instantiates only the fragment's nodes into the live graph (the loader's per-node construction already exists; the in-process clip_instantiate shows the shape). Delete the temp-file staging and the restore-on-failure double load.


## F430 [UNVERIFIED] medium (reporter: medium) workaround-hack — tools/mforce_ui/main.cpp:11442

**Temp files on disk are the graph-merge mechanism for paste/copy, and a dead temp-file playback path plus stale comments survive the 2026-09-13 unification**

Reporters: arch-ui-tools

Evidence: `paste_fragment_text: `{ std::ofstream f(merged); f << doc.dump(2); } try { load_graph_from_path(merged); } catch (...) { try { load_graph_from_path(stage); } ...` (11442-11448) after staging the whole graph to %TEMP% (11349-11357); copy_selection_to_clipboard saves the entire graph to a temp file and re-reads it to pick out nodes (11296-11303). get_playback_patch_path (3502-3526) still writes %TEMP%/mforce_playback.json but has no callers (grep: definition only). Stale: 3542 '// Audio: poll-driven streaming via waveOut', 4590-4591 'Audio path routes through load_instrument_patch(temp)', 8301-8306 'via get_playback_patch_path'.`

Why: Merging via the filesystem is slow, non-atomic, resets editor identity (load_graph_from_path sets s_nextId = 1 at 1588 and rebuilds every node), and exists only because there is no in-memory document model with a merge operation. Dead code and stale comments mislead the next reader about where audio actually comes from.

Recommendation: In-memory merge on the typed document (from the shared-codec finding); delete get_playback_patch_path and the three stale comments.


## F431 [UNVERIFIED] high (reporter: high) soundness — tools/mforce_ui/main.cpp:11481

**show_node_context_menu reads a GraphNode* after emplace_back/delete_node invalidated it (use-after-realloc / use-after-erase)**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `11481-11482 `GraphNode* node = nullptr; for (auto& n : s_nodes) if (n.id == s_contextNodeId) { node = &n; break; }`; Duplicate: 11513 `s_nodes.emplace_back(node->typeName);` then 11518-11519 `node->inputs[i].defaultValue`, 11525 `node->settingValues`, 11532 `GetNodeScreenSpacePos(node->id)`; Replace-with 11553 `replace_node_with(targetId, t)` (emplace_back + delete_node) and Delete 11567 `delete_node(s_contextNodeId)` are followed in the same frame by 11575-11576 `node->id == s_listenTapNode; !node->outputs.empty() && node->typeName`, 11611 `sel[i] == node->id`, 11669 `group_of(node->label)`. delete_node erases via remove_if (1167-1170); s_nodes is a std::vector with no reserve.`

Why: After emplace_back reallocates, `node` points into freed storage (use-after-free read); after delete_node, it points at a moved-from successor or past the live range. ImGui::MenuItem closes the popup but the rest of the function still executes this frame, so every mutating item (Duplicate, Replace with, Delete) is followed by reads through the stale pointer. The same file documents this exact hazard three times (10580, 10687, 11164, 11239) but this function was not given the same treatment.

Recommendation: After any mutating MenuItem (Duplicate, Replace-with action, Delete, Ungroup), `ImGui::EndPopup(); return;` immediately; or re-find `node` by id after each mutation. Longer term, make Duplicate a call into the single node-copy routine (see duplication finding) so the context menu never touches s_nodes directly.


## F432 [UNVERIFIED] high (reporter: high) duplication — tools/mforce_ui/main.cpp:11507

**Four divergent implementations of 'copy a node's state' (Duplicate, clip_instantiate, replace_node_with, saver/loader fragment)**

Reporters: tools-ui-chunk5-L9196-11693

Evidence: `(a) 11517-11529 Duplicate copies pins and settings BY INDEX only: `for (int i = 0; i < node->inputs.size() && i < dup.inputs.size(); ++i) dup.inputs[i].defaultValue = ...` — no arrays, formantRows, Envelope stages, dynamicPins, curveKnots, jsonExtras, no update_node_dsp, no group membership; (b) 10286-10404 clip_node_state/clip_instantiate: pins, settings, arrays, formants, stages, seed, polyphony, chPins; (c) 11178-11191 replace_node_with: pins, settings, dynamicPins by name; (d) 11273-11471 serialize whole graph → fragment → merge → load_graph_from_path.`

Why: Duplicate of an ExplicitPartials, FormantSpectrum, Envelope or Curve node silently produces a node missing its arrays/formants/stages/knots — a different result from Copy/Paste of the same node. Each new node field (sustaining/onsets/glideMs were added recently) must be remembered in up to four places; today clip_node_state carries sustaining/onsets but Duplicate and replace_node_with do not.

Recommendation: Implement one `GraphNode::copy_state_from(const GraphNode& src, MatchBy by)` (by name) that covers every serialized field, and make Duplicate, clip_instantiate and replace_node_with call it. Better still, express Duplicate as 'copy selection to fragment + paste' once the single clipboard exists.


## F433 [UNVERIFIED] high (reporter: high) soundness — tools/mforce_ui/main.cpp:11739

**Waveform mip cache keyed on buffer pointer + 3-sample fingerprint can serve a stale display after a regenerate**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `11739-11744: `static std::unordered_map<const float*, WaveMip> s_waveMips; const float f0 = buf[0], f1 = buf[sampleCount / 2], f2 = buf[sampleCount - 1]; ... if (mip.count != sampleCount || mip.fp[0] != f0 || mip.fp[1] != f1 || mip.fp[2] != f2)`. The Output buffer is refilled in place: 13597 `g_outputWaveform.clear();` then 4415 `g_outputWaveform.assign(size_t(frames), 0.0f);` reuses capacity, so the pointer key is identical across renders.`

Why: Renders are deterministic (seeds live in JSON). buf[0] is the first attack sample (0) and buf[N-1] the release tail (~0) for essentially every note, so the fingerprint reduces to one mid-buffer sample plus length. Tweak a parameter that only affects the attack (envelope attack time, onset noise) and regenerate the same note/duration: steady-state sample at N/2 is bit-identical, the cache hit stands, and both the peak labels and every column at zoom >= 64 (which Fit selects for any multi-second buffer) draw the OLD waveform. That silently breaks the project's core 'tweak parameter, see updated waveform' loop for exactly the edits a user is most likely auditioning.

Recommendation: Drop content fingerprinting. Have the producer own the mip: generate_unified builds a WaveMip next to g_outputWaveform and per GraphNode::waveformData when it writes them (or bump a global g_waveGeneration counter there and key the cache on {buf, generation}). The renderer then never guesses whether data changed.


## F434 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:11874

**Spectrum sample rate hard-coded to 48000 instead of AUDIO_SAMPLE_RATE**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `11874: `g_outputSpectrumSR = 48000;  // matches the offline-render SR convention` and 3592 `static int g_outputSpectrumSR = 48000;` while 3547 defines `static constexpr int AUDIO_SAMPLE_RATE = 48000;` and --gencheck/--dump-playback write WAVs with it (13084, 13308).`

Why: The frequency axis (11916, 11969, 11976) is derived from this value; a future rate change (the memory notes a 'rate-bake audit') silently mislabels every spectrum peak.

Recommendation: Use AUDIO_SAMPLE_RATE (or the render context's rate from generate_unified) and drop the separate global.


## F435 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:11922

**Log-frequency axis, freq<->x mapping, hover readout box, pop-out button and early-return blocks are duplicated across spectrum, partials and formant drawers**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `freq_to_x/x_to_freq lambdas 11922-11933 == 12356-12367; decade grid loop with `static const float decMul[] = {1.0f, 2.0f, 5.0f};` 11939-11955 == 12370-12386; hover label box (`CalcTextSize` ... `AddRectFilled(ImVec2(lblX - 3, lblY - 1) ... IM_COL32(0, 0, 0, 200)` ... `AddText(... IM_COL32(255, 255, 200, 255)`) at 12004-12017, 12222-12228, 12445-12451; pop-out ArrowButton block 11815-11824 is the body of draw_strip_popout_button 12026-12036 which draw_waveform does not call; `(node not initialized)` early-return 12057-12064 == 12257-12264 and `(empty)` 12087-12094.`

Why: Roughly 120 lines of triplicated plot chrome; the formant grid already differs from the spectrum grid (labels on 1x only vs 1x and 2x) with no stated reason, and the three hover boxes clamp differently (spectrum flips the label left of the cursor, the others clamp). Each new strip kind copies the set again.

Recommendation: A small `LogAxis { float x0, x1, fMin, fMax; float to_x(float f) const; float to_f(float x) const; void draw_grid(ImDrawList*, float y0, float y1, bool label2x) const; }`, a `draw_hover_box(ImDrawList*, ImVec2 mouse, const char* text, ImVec2 plotMin, ImVec2 plotMax)`, and a `draw_strip_frame(...)` for background/label/border/early-return. Have draw_waveform call draw_strip_popout_button.


## F436 [UNVERIFIED] low (reporter: low) workaround-hack — tools/mforce_ui/main.cpp:12168

**Partials scrubber undoes rolloff with pow() to recover weights it discarded ten lines earlier**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `12168-12172 comment: `We need the ORIGINAL ampl (pre-rolloff) ... The cheap workaround: undo rolloff with the per-column rolloff exponent`; 12177-12178 `float w1 = ampl1[i] * std::pow(pmult1, ro1); float w2 = ampl2[i] * std::pow(pmult2, ro2);` after apply_rolloff mutated ampl1/ampl2 in place at 12084-12085.`

Why: Self-described workaround: two extra pow() per partial per frame and a float round-trip that reintroduces error, purely because the raw arrays were overwritten instead of kept.

Recommendation: Keep `rawAmpl1/rawAmpl2` (the get_array results) and compute `dispAmpl` separately; the scrub path reads the raw arrays directly.


## F437 [UNVERIFIED] high (reporter: high) rt-safety — tools/mforce_ui/main.cpp:12342

**draw_formant_strip advances the live node's formant from the UI thread while the audio thread may be pulling the same object**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `12342: `ifp->fmt_next();` on `node->dspSource.get()` (12256). formant.h:82 `fmt_next()` does `frequency_->next(); gain_->next(); width_->next(); power_->next();` and rewrites loFreq_/hiFreq_. play_continuous 4925: `sc.source = sn->dspSource.get();` hands the same node objects to audio_callback (3961 `ch.source->next()`), and full_additive_source.cpp:51 `formant_->fmt_next()` ticks the formant per sample on that thread.`

Why: Every frame the Waveforms window is visible, each FormantSpectrum/FormantSequence node (fp is null for those, so they always take the fallback branch) and every un-snapshotted Formant has its parameter ValueSources advanced once per UI frame with no g_audioMutex. During node-graph (or patch-graph g_streamSource) streaming this is a cross-thread write of ValueSource state and the formant's cached band edges against concurrent audio-thread reads: a data race that can glitch audio and, independent of streaming, drives any envelope/LFO wired to a formant forward at UI frame rate.

Recommendation: Never call next()/fmt_next() from draw code. Compute the fallback curve from the formant's current() values via the same local formant_gain_at used by the snapshot path (for FormantSpectrum, iterate its child formants' current cf/gn/wd/pw), or have generate_unified capture a gain-curve snapshot per formant node like it does for evo snapshots. If the live state must be read, take g_audioMutex and read only.


## F438 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:12469

**Assorted nits in the range: unused local, misnamed local, comment drift, triplicated basename extraction, no-op dynamic_cast, shared_ptr copied per call, triple map lookup**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `12469 `float waveAreaH = ImGui::GetContentRegionAvail().y;` never read (MSVC C4189). 13558 `bool s_dockLayoutInitialized = false;` is a plain local with the static prefix. 12970-12976 comment describes `Headless round-trip` but sits above --dump-curve-opts; the real --roundtrip at 13103 has no comment. Basename by `strrchr(..., '/')`/`strrchr(..., '\\')` at 13626-13630, 13670-13673 and 14582-14583 instead of std::filesystem::path::filename(). 13023 `dynamic_cast<PitchedInstrument*>(ip.instrument.get())` on a `std::unique_ptr<PitchedInstrument>` (patch_loader.h:20) is a no-op, and 13283's `throw std::runtime_error("not a PitchedInstrument")` is a mislabeled null check. 12300-12301 `get_at` takes `std::shared_ptr<ValueSource> fallback` by value (four refcount round-trips per formant strip per frame). 11742/11750/11765 look up `s_waveMips[buf]` three times.`

Why: Each is small, but together they are the kind of residue a public release review flags: compiler warnings, misleading names/comments, and copy-pasted path handling where the standard library already does it.

Recommendation: Delete waveAreaH; rename to dockLayoutInitialized; move the comment to --roundtrip; replace the three strrchr blocks with `std::filesystem::path(p).filename().string()`; drop the no-op cast and fix the error text; take `fallback` by const reference; hoist one `WaveMip& m = s_waveMips[buf]` reference.


## F439 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:12472

**Zoom/Fit/scrollbar/wheel/ctrl-wheel handling is copied between draw_waveform_window and draw_wave_popouts**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `12472-12512 (Zoom arrows `g_waveZoom = std::max(1, g_waveZoom / 2)` ... `std::min(4096, g_waveZoom * 2)`, Fit, `SliderInt("##hscroll"`) and 12616-12632 (wheel: `if (ImGui::GetIO().KeyCtrl) { if (wheel > 0.0f && g_waveZoom > 1) ...`) versus 12719-12744 (`p.zoom = std::max(1, p.zoom / 2)` ... `SliderInt("##pop_hscroll"`) and 12755-12769 (identical wheel block on `p.zoom`/`p.scroll`). The pop-out already has the right shape (`WavePopout{zoom, scroll}` at 3578) but the main window keeps the same two fields as the globals g_waveZoom/g_waveScrollPos.`

Why: Two copies of the zoom clamps (1..4096), step math and wheel semantics have already drifted (main window has keyboard shortcuts and click-drag at 12634-12674, pop-outs do not; Fit width differs). The scroll-range bug above had to be fixed in two places and was not.

Recommendation: Introduce `struct WaveView { int zoom{1}; int scroll{0}; }`, make the main window's view a `static WaveView g_mainWave` (replacing g_waveZoom/g_waveScrollPos), and write `wave_view_controls(WaveView&, int sampleCount, int visiblePx)` and `wave_view_handle_input(WaveView&, ...)` used by both call sites.


## F440 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:12503

**Scroll range and Fit zoom ignore the strip margin and column width, so the tail of every buffer is unreachable (and Fit cuts it off)**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `12503: `int maxScroll = std::max(0, g_waveformSamples - (int)(waveAreaW) * g_waveZoom);` and 12484/12669: `g_waveZoom = std::max(1, g_waveformSamples / (int)waveAreaW);` but the drawn width is `colW - WAVE_MARGIN_W` where 12571 `colW = (waveAreaW - (N - 1) * gap) / float(N)` and 11715 `drawW = width - WAVE_MARGIN_W` (45 px), 11780 `pixelCount = (int)drawW`. Pop-outs repeat it: 12737 `maxScroll = std::max(0, count - (int)waveW * p.zoom)` vs the same 45 px margin, and 12731 Fit divides by `innerW` (width remaining after the Zoom widgets) rather than `waveW`.`

Why: At max scroll the last visible sample is `samples - (waveAreaW - drawW) * zoom`. With one column that is 45*zoom samples (184k samples, ~3.8 s, at zoom 4096); with Columns=2 roughly half the buffer can never be scrolled into view, and Fit shows only the first half. The scroll slider, wheel, End key and Fit all share the wrong bound.

Recommendation: Compute the visible sample width once from the actual strip draw width: `int visiblePx = int(colW - WAVE_MARGIN_W); maxScroll = max(0, samples - visiblePx * zoom); fitZoom = max(1, samples / visiblePx)`; pass that into both the main window and pop-outs via the shared view helper suggested in the controls-duplication finding.


## F441 [UNVERIFIED] low (reporter: low) soundness — tools/mforce_ui/main.cpp:12649

**Waveform-window keyboard shortcuts fire while a text input in the same window is active**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `12649: `if (ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) {` followed by `IsKeyPressed(ImGuiKey_Minus ...)`, `IsKeyPressed(ImGuiKey_0 ...)`, `IsKeyPressed(ImGuiKey_Equal ...)` with no `WantTextInput` check, whereas the global shortcuts at 13830/13836 guard with `!ImGui::GetIO().WantTextInput`. 13481 enables `io.ConfigDragClickToInputText = true`, so a click on the Scrub slider (12500) or the scroll slider (12508) enters text entry.`

Why: Typing '0', '-' or '=' into the scrub value while the mouse hovers the window also fits/zooms the view; '0' additionally resets scroll.

Recommendation: Add `&& !ImGui::GetIO().WantTextInput` to the condition at 12649 (and to the pop-out wheel handling if it gains shortcuts).


## F442 [UNVERIFIED] low (reporter: low) duplication — tools/mforce_ui/main.cpp:12801

**Crash logger duplicates DbgHelp initialisation and the log header between crash_log_stack and seh_crash_filter**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `12801-12806 `static bool symInited = false; if (!symInited) { SymSetOptions(...); SymInitialize(proc, NULL, TRUE); symInited = true; }` repeated verbatim at 12863-12868 with a second, independent static; header `fprintf(f, "\n========================================\n"); crash_log_timestamp(f);` at 12834-12835 and 12845-12846.`

Why: Two statics means SymInitialize can be called twice in one process (second call fails with ERROR_INVALID_PARAMETER, harmlessly today); the symbol-buffer setup (12808-12811 vs 12869-12872) is also copied, so a MaxNameLen change must be made twice.

Recommendation: One `static void ensure_sym_init()` and one `static void crash_log_open_header(FILE*, const char* kind)`; seh_crash_filter then only adds the exception-record lines.


## F443 [UNVERIFIED] high (reporter: high) god-class — tools/mforce_ui/main.cpp:12948

**main() is a 1,711-line function: 12 inline headless CLI modes, GLFW/ImGui/audio/MIDI init, dock layout, menu bar and the whole frame loop in one body inside a zero-indented 1,200-line try**

Reporters: arch-ui-tools, tools-ui-chunk6-L11694-13843

Evidence: `12948 `int main(int argc, char** argv) {` ... 13442 `    try {` followed by 13443 `    if (!glfwInit()) return 1;` at the same indentation, closed only at 14649 `    } catch (const std::exception& e) {`. Headless dispatch occupies 12977-13440 as a chain of `if (argc >= N && std::string(argv[1]) == "--mode")` blocks (12977, 13010, 13018, 13046, 13103, 13121, 13146, 13171, 13199, 13224, 13255, 13332, 13365).`

Why: Nothing in main() can be unit-tested or reasoned about in isolation; the headless harness that exists to verify engine behaviour is welded to GUI boot code, so adding a mode means editing the GUI entry point. The unindented try block hides the function's real nesting and makes the catch at 14649 easy to miss. Function-local statics inside the loop body (13572-13573) are the symptom of state that has no home.

Recommendation: Split into (a) `headless.cpp` with a `struct HeadlessMode { const char* flag; int minArgc; int (*run)(int, char**); }` table and `int run_headless(int argc, char** argv)` returning -1 when no flag matched; (b) `app_init()` / `app_frame()` / `app_shutdown()` for the GUI; (c) a `MenuBarActions` drawer. main() becomes ~30 lines: console attach, headless dispatch, GUI run under one try.

Also reported as: Single 14,658-line TU with 88 file-scope mutable statics; main() is 1,711 lines holding ten headless modes and the whole node-editor interaction loop


## F444 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:12978

**Ten headless modes repeat the identical context/registry prologue and try/catch-print-return epilogue; mono-to-stereo widening copied twice**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `Prologue `s_headless = true; ImGui::CreateContext(); ImNodes::CreateContext(); register_all_sources();` at 12978-12981, 13047-13050, 13104-13107, 13122-13125, 13147-13150, 13172-13175, 13200-13203, 13225-13228, 13256-13259, 13333-13336, 13366-13369. Epilogue `} catch (const std::exception& e) { fprintf(stderr, "<mode> failed: %s\n", e.what()); return 1; }` at 13111-13114, 13136-13139, 13158-13161, 13211-13214, 13238-13241, 13318-13321, 13351-13354, 13435-13438. Stereo widening 13080-13083 `stereo[size_t(i)*2] = stereo[size_t(i)*2+1] = g_outputWaveform[size_t(i)];` duplicated at 13303-13307. Also 12975 `// TEMP DEBUG:` mode shipped in main.`

Why: ~465 lines where ~150 would do; every new harness copies the scaffold and the ad-hoc `argc >= N` guards, and the `--dump-curve-opts` TEMP DEBUG mode (12975-13006) has become permanent. Error-handling behaviour (what is printed, which exit code) is only consistent by copy discipline.

Recommendation: Table-driven dispatch (see the god-class finding): one `headless_begin()`, one wrapper that catches and prints `<flag> failed: ...`, each mode a small function taking a parsed `HeadlessArgs`. Add `static std::vector<float> widen_mono(const std::vector<float>&)` or make write_wav accept mono. Delete or promote the TEMP DEBUG mode.


## F445 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:13054

**--gencheck re-implements the engine's v2 score parsing instead of calling it**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `13055-13059 comment: `Mirrors the engine loader's v2 score reading exactly ... gencheck parity depends on the two ends reading one score the same way.` 13061-13072 re-types prevEnd/time/onset/hold reading. The engine's version is engine/src/patch_loader.cpp:1601-1610 (`float prevEnd = 0.0f; ... noteJson.contains("time") ? ... : prevEnd; bool hold = noteJson.value("hold", false); std::string onset = noteJson.value("onset", std::string());`) and already diverges: the engine uses `.at("note")`/`.at("duration")` (throws on absence) while the UI copy defaults to 60/1.0.`

Why: The comment itself names the hazard: the acceptance check for render-capture unification only means something if both ends parse identically, and they already do not (missing-field behaviour differs). The next score-schema change (the spec is dated 2026-09-20 and evolving) has to be made twice.

Recommendation: Expose `std::vector<ScoreEvent> parse_score_events(const nlohmann::json&)` from the engine (patch_loader) and have both the loader and --gencheck (and any UI Note-tab path that builds SchedNote from s_loadedScore) call it.


## F446 [UNVERIFIED] high (reporter: high) duplication — tools/mforce_ui/main.cpp:13284

**--dump-playback and --dump-stream hand-copy the audio callback's mixing arithmetic (and use a different voice entry point than the live path) while documenting themselves as exact replicas**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `13358-13359 comment: `run the exact play_continuous + audio_callback stream mixdown offline`; 13389-13421 re-types the per-sample mono stream, gainL/gainR, equal-power pan `std::cos(t * 0.5f * PI)` / `std::sin(...)`, and `soft_clip` that audio_callback already has at 3939-3971. 13286-13290 re-types the voice loop `performSource->tick(); ... sv.source->next() * sv.gain; for (auto& a : sv.advanceList) a->next();` from 3889-3899 but omits the fade/ring-out logic (3894-3917) and calls `pitched->prepare_voice(noteNum, vel, dur)` (13284) whereas the real live keyboard path uses `prepare_voice_at(slot, ...)` (4691, 4801).`

Why: These modes exist for UI-vs-CLI mismatch debugging; their only value is bit-parity with what the callback does. Any change to the callback (a new gain stage, pan law, ring-out) silently diverges the dump, and the keyboard dump already differs from the live path it claims to reproduce (round-robin slot selection, no cap fade). Conclusions drawn from the dumps can be wrong without any warning.

Recommendation: Factor the callback body into RT-safe inline helpers it and the dumps both call: `static inline float voice_render_sample(Voice&)` and `static inline void mix_stream_sample(float& sL, float& sR)` (no allocation, no locks; the callback keeps the lock around the loop). The dump modes then drive the same code offline. Have --dump-playback --keyboard call the same note-on helper the keyboard panel uses so slot selection and fade are shared.


## F447 [UNVERIFIED] medium (reporter: medium) workaround-hack — tools/mforce_ui/main.cpp:13571

**Audio device is torn down and reopened on every window refocus after >2 s away, plus a manual 'Restart audio' menu item, as substitutes for device-change notification**

Reporters: tools-ui-chunk6-L11694-13843

Evidence: `13576-13577: `if (!wasFocused && focused && glfwGetTime() - unfocusedAt > 2.0) g_audioRestartRequest.store(true);` 13755-13760 comment: `Manual escape hatch for Bluetooth endpoints ... Automatic layers (watchdog, refocus restart) can't time that; the user can.` audio_watchdog 4092-4094 services the request with `shutdown_audio(); if (init_audio(/*quiet=*/true))`, i.e. stopStream/closeStream/RtAudio destruct (4069-4075) then a new RtAudio + openStream + startStream (4015-4054).`

Why: A healthy stream is reopened every time the user alt-tabs back after two seconds, whether or not anything changed; if a continuous stream or held notes are sounding, output drops for the reopen duration and the transport status flashes 'restarted'. Three layers (heartbeat watchdog, refocus heuristic, manual menu) are stacked to guess at an event the OS will deliver: WASAPI publishes default-device and device-state changes via IMMNotificationClient, and RtAudio has an error callback already registered (4026).

Recommendation: Subscribe to IMMNotificationClient::OnDefaultDeviceChanged/OnDeviceStateChanged (Windows-only is in scope) and set g_audioRestartRequest from that; keep the heartbeat watchdog for silent death and the menu item as the explicit fallback, and delete the refocus heuristic. If the BT profile-transition case cannot be observed, restart on refocus only when a stream is NOT currently sounding.


## F448 [UNVERIFIED] high (reporter: high) god-class — tools/mforce_ui/main.cpp:13848

**The entire Node Editor window (~675 lines) is inline in main()'s frame loop while every sibling panel is a draw_* function**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `13848 `ImGui::Begin("Node Editor", nullptr, ...` through 14522 `ImGui::End(); // Node Editor` sits directly in the loop body, immediately followed by 14527 `draw_transport_panel();`, 14532 `draw_properties_panel();`, 14537 `draw_mappings_dialog();`, 14540 `draw_shape_editor();`, 14545-14561 draw_waveform_window/draw_wave_popouts/draw_spectrum_window/draw_keyboard_panel/draw_audition_window. Inside the inline body: hidden state as function-local statics (13913-13914 `static std::vector<std::string> prevPath; static std::vector<ImVec2> panStack;`, 14185 `static std::unordered_set<int> prevSelection;`), six lambdas (frame_level 13916, project_entity 13997, draw_span_for 14080, translate 14264, fitAllNodes 14427), and at least nine independent concerns (breadcrumb, drill camera, node/group draw, link projection, dyn-pin wires, wormhole overlay, wheel/arrow/F/Ctrl-click input, link create/destroy, context menus, delete, status bar).`

Why: main() is already 1,711 lines; this block is the largest single reason. State hidden in loop-body statics cannot be reset on New/Load (prevPath/panStack survive a file load, so the first path change after a load may restore a pan from the previous document), cannot be unit-tested, and is invisible to the headless CLI modes. The accretion pattern is also how the duplicate shortcut bindings (14253 vs 14461) and the missing DSP rewire on Delete escaped notice: there is no single place that owns 'editor input'.

Recommendation: Extract `draw_node_editor()` with a small `NodeEditorState` struct (prevPath, panStack, prevSelection, s_groupProj) owned at file scope and reset by new_graph/load. Inside it, split into draw_breadcrumb(), update_drill_camera(), draw_projected_links(), draw_wormhole_overlay(), handle_editor_input() (all keyboard/mouse chords in one place), handle_link_events(), draw_editor_status_bar(). This also removes the need for the function-local statics.


## F449 [UNVERIFIED] low (reporter: low) soundness — tools/mforce_ui/main.cpp:13939

**Drill camera panStack goes stale after a non-prefix path change (wormhole jump), and the deeper branch fills then overwrites the same slot**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `13941-13944: `panStack.resize(prevPath.size() + 1, ImNodes::EditorContextGetPanning()); panStack[prevPath.size()] = ImNodes::EditorContextGetPanning();` (resize-with-fill followed by assignment of the same value to the same slot). 13950-13952: the `else { frame_level(); }` branch for a non-prefix move leaves panStack untouched, although the comment at 13910-13911 assumes 'Breadcrumb-only moves are always prefix moves, so the two cases cover everything' — the wormhole jump at 14150 `s_groupPath.assign(chain.rbegin(), chain.rend());` is not a breadcrumb move.`

Why: After jumping from [A,B] to [C,D] via a wormhole, backing out to [C] restores panStack[1], which is the pan recorded inside A, not anything related to C — a visible 'blank pane' regression of the exact symptom the camera was built for. The double GetPanning call is a nit.

Recommendation: In the non-prefix branch, `panStack.clear()` (or truncate to the common-prefix length) before frame_level(); replace the resize+assign pair with a single `panStack.resize(prevPath.size()); panStack.push_back(pan);`.


## F450 [UNVERIFIED] medium (reporter: medium) duplication — tools/mforce_ui/main.cpp:13997

**'Visible representative at this drill level' ancestor walk is written four times (one copy dead) and the wormhole ghost overlay is written twice**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `Ancestor walk: 13997-14004 lambda `project_entity` (`while (g && !visible_at_path(g->name)) g = group_of(g->name); return g ? g->editorId : -1;`) is defined and never called (grep: only the definition); identical logic at 14082-14088 inside draw_span_for, at 14123-14129 inline in the hover branch, and at 9122-9128 in project_pin. Overlay: draw_span_for 14090-14099 (`dl->AddLine(ImVec2(p0.x + d0.x * 0.5f, ...), IM_COL32(140, 170, 200, 110), 2.0f); dl->AddRect(p1, ..., IM_COL32(140, 170, 200, 200), 6.0f, 0, 2.0f);`) is repeated verbatim at 14131-14143 instead of calling `draw_span_for(*hn, *twin)`.`

Why: Dead code in the hottest UI function misleads readers (it looks like the link loop's projector), and four copies of the walk mean the next change to group visibility semantics (e.g. nested-group rules) must be found and applied in four places; the overlay copy is already 14 lines of pure redundancy.

Recommendation: Add a file-scope `static int visible_entity_for(const GraphNode& n)` (node id when visible, else nearest visible ancestor group's editorId, else -1) used by project_pin, draw_span_for and the hover branch; delete project_entity. Replace 14130-14144 with `draw_span_for(*hn, *twin);`.


## F451 [UNVERIFIED] medium (reporter: medium) efficiency — tools/mforce_ui/main.cpp:14005

**Per-frame link/visibility pass is O(links x nodes x pins) plus string-keyed group lookups, and redoes lookups it already has**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `Per link per frame: 14006 `Pin* sp = find_pin(link.startPinId);`, 14009 `find_pin(link.endPinId)`, 14015-14016 two `find_node_for_pin(...)`, 14019-14020 two `project_pin(...)` each doing another find_node_for_pin + visible_at_path + group_of walk (9122-9128). find_pin/find_node_for_pin are linear over every node's inputs and outputs (806-820); visible_at_path -> group_of is a linear string-compare scan over every group's member list (689-702) and is also called per node (13972), per group (13985) and per dynamic pin (14043). 14045-14049 iterates nlohmann::json per frame with `val.value("ref", std::string())` and a label string scan of s_nodes. 14101-14104 calls wormhole_twin (831-840, itself O(links x nodes x pins)) for each selected wormhole every frame. One-shot redundancy in the link-created handler: 14334 `Pin* inPinObj = find_pin(inPin);` repeats inPinDesc from 14313; 14347-14348 repeat outNode/inNode from 14311-14312.`

Why: Every frame, for a 100-node/150-link patch, this is on the order of 10^6 integer compares and 10^4-10^5 std::string compares before any drawing happens, scaling quadratically with graph size — the UI frame budget is being spent on index lookups that a pin->node map and a label->group map would make O(1). UI-thread only, so not an RT-safety violation, but the editor is the one window that is always open.

Recommendation: Maintain `std::unordered_map<int, GraphNode*> s_pinOwner` and `std::unordered_map<std::string, NodeGroup*> s_groupOfLabel` rebuilt on graph mutation (there are only ~10 mutation sites, all already calling mark_graph_dirty/update_all_dsp), and compute each node's `visible` flag once per frame in the 13971 loop, reusing it for links. Reuse inPinDesc/outNode/inNode in the create handler.


## F452 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:14145

**Wormhole double-click 'jump to twin' centering is overwritten by the drill camera on the next frame whenever the twin is at a different drill level**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `14150 `s_groupPath.assign(chain.rbegin(), chain.rend());` then 14154-14157 `ImNodes::EditorContextResetPanning(ImVec2(ImGui::GetWindowWidth() * 0.5f - gp.x, ...))`. Next frame, 13915 `if (prevPath != s_groupPath) {` fires and every branch resets panning: 13945 `frame_level();` (deeper), 13947-13948 `ImNodes::EditorContextResetPanning(panStack[s_groupPath.size()]);` (shallower), 13951 `frame_level();` (else). frame_level pans to top-left-of-content (13932-13933), not to the twin.`

Why: The jump only centers on the twin when the twin is at the same level as the hovered half (then prevPath == s_groupPath and the detector is silent). In the common case (twin inside another group, which is what wormholes are for) the user lands on a top-left framing of the twin's level and must hunt for the twin — the exact 'F-and-hunt' the drill camera was added to remove (13904-13906).

Recommendation: Give the drill camera an explicit override: e.g. a file-scope `std::optional<ImVec2> s_pendingPan` set by the jump; the detector applies it (and still maintains panStack) instead of frame_level when present. Or route all path changes through one `set_group_path(path, CameraIntent)` function so the jump's intent survives.


## F453 [CONFIRMED-BY-MATT'S-CLAUDE] high (reporter: high) duplication — tools/mforce_ui/main.cpp:14253

**Ctrl+C / Ctrl+V are bound twice to two separate clipboard subsystems; a single Ctrl+V pastes the selection twice**

Reporters: arch-ui-tools, tools-ui-chunk7-L13844-14658

Evidence: `14253-14256: `if (editorHovered && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput) { if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copy_selection_to_clipboard(); if (ImGui::IsKeyPressed(ImGuiKey_V, false)) paste_clipboard();` and 14461-14464: `if (editorFocused && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) { if (ImGui::IsKeyPressed(ImGuiKey_X)) clipboard_cut(); if (ImGui::IsKeyPressed(ImGuiKey_C)) clipboard_copy(); if (ImGui::IsKeyPressed(ImGuiKey_V)) clipboard_paste();`. The first pair is the 2026-09-07 OS-clipboard design (11258-11267 banner; 11317 `ImGui::SetClipboardText(frag.dump().c_str())`; 11473-11474 `paste_fragment_text(ImGui::GetClipboardText())`). The second pair is the older in-process `s_clipboard` json (10406-10545; 10455 `s_clipboard = json{...}`; 10463 `if (!clipboard_has_content()) return;`). A hovered editor is normally also the focused window, so both conditions hold on the same frame.`

Why: On Ctrl+C both copies run (the OS-clipboard one re-serializes the whole graph to a temp file, 11296-11300, and each overwrites the other's status message). On Ctrl+V paste_clipboard merges-then-reloads the graph from the OS fragment, then clipboard_paste instantiates the same nodes again from s_clipboard: the user gets two copies of every pasted node, offset differently, and two update_all_dsp rebuilds. Two complete clipboard implementations (~140 + ~200 lines) are also pure redundancy; the 09-07 banner states the OS-clipboard one is the design.

Recommendation: Keep one binding block. Delete the in-process subsystem (clipboard_has_content/clipboard_copy/clipboard_paste/clipboard_cut, s_clipboard, s_clipPasteCount, clip_node_state/clip_instantiate if unused elsewhere) and implement Cut as copy_selection_to_clipboard + delete of the selection + update_all_dsp. Gate the surviving block on editorFocused (the stricter of the two) and use the no-repeat IsKeyPressed(key, false) form.

Also reported as: Two independent clipboard subsystems are both bound to Ctrl+C/Ctrl+V in the same frame, so a paste fires twice


## F454 [CONFIRMED-BY-MATT'S-CLAUDE] high (reporter: high) duplication — tools/mforce_ui/main.cpp:14253

**Two independent node clipboards, and both key handlers fire on the same Ctrl+C / Ctrl+V**

Reporters: arch-duplication

Evidence: `14253: `if (editorHovered && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput) { if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copy_selection_to_clipboard(); if (ImGui::IsKeyPressed(ImGuiKey_V, false)) paste_clipboard(); }`  14461: `if (editorFocused && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) { if (ImGui::IsKeyPressed(ImGuiKey_X)) clipboard_cut(); if (ImGui::IsKeyPressed(ImGuiKey_C)) clipboard_copy(); if (ImGui::IsKeyPressed(ImGuiKey_V)) clipboard_paste(); }``

Why: Clipboard A (process-local JSON: clip_node_state/clip_instantiate 10286-10404, clipboard_copy/paste/cut 10411-10545, ~260 lines, its own per-node serializer that re-implements the load order of load_graph_from_path 1773-1935) and clipboard B (OS clipboard fragment through the real saver/loader: copy_selection_to_clipboard/paste_fragment_text 11273-11471, ~200 lines) coexist. With the editor hovered and focused (the normal state after clicking in it) one Ctrl+C fills both, and one Ctrl+V runs paste_fragment_text AND clipboard_paste, instantiating the selection twice through two different code paths. The context-menu Duplicate (11507-11534) is a third per-node copier that copies pins/settings by INDEX and silently drops arrays, formant rows, curve knots and envelope stages. Three copy mechanisms with three fidelity levels.

Recommendation: Keep only the saver-backed fragment path (it cannot drift from the file format, as its own comment says). Delete clip_node_state/clip_instantiate/clipboard_copy/paste/cut; route the Edit menu (13738-13743), the Ctrl shortcuts (one handler, 14461) and Duplicate through copy_selection_to_clipboard/paste_fragment_text.


## F455 [UNVERIFIED] low (reporter: low) duplication — tools/mforce_ui/main.cpp:14365

**Gold-wire 'unwire dynamic pin' block duplicated between IsLinkDestroyed and the Delete-key handler**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `14365-14371: `if (GraphNode* dn = find_node_for_dyn_link(destroyedLinkId, &setting)) { dn->dynamicPins[setting] = nullptr; dn->apply_config(); mark_graph_dirty(); }` and 14489-14494: `if (GraphNode* dn = find_node_for_dyn_link(lid, &setting)) { dn->dynamicPins[setting] = nullptr; dn->apply_config(); mark_graph_dirty(); continue; }`.`

Why: The semantics ('pin stays promoted, stowed scalar takes over') are documented at both sites and must be kept in sync by hand; delete_node's demotion path at 1147-1163 is a third variant of the same operation with a different outcome (ref dropped), which is exactly the kind of drift a helper prevents.

Recommendation: Add `static bool unwire_dyn_link(int linkId)` returning whether the id was a dynamic-pin wire; both sites become `if (unwire_dyn_link(id)) ...`.


## F456 [UNVERIFIED] low (reporter: low) smell — tools/mforce_ui/main.cpp:14400

**Right-click handler has an empty if-branch with collapsed indentation and a loop whose body does not depend on the loop variable**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `14400-14401: `if (linkRouted) { } else {` with the else body un-indented to the outer level; 14403-14406: `int hoveredNode = -1; for (auto& n : s_nodes) { if (ImNodes::IsNodeHovered(&hoveredNode)) break; }` — IsNodeHovered does not use n, so the loop is either one call or an N-fold repeat of the same call (and on an empty graph is never evaluated at all).`

Why: Reads as unfinished code; the loop suggests a per-node check that is not happening, and the empty-then branch inverts the natural condition purely to avoid re-indenting. Harmless at runtime (IsNodeHovered is idempotent), but it is the kind of residue that hides real bugs nearby.

Recommendation: `if (!linkRouted) { int hoveredNode = -1; if (ImNodes::IsNodeHovered(&hoveredNode)) { s_contextNodeId = hoveredNode; s_wantNodeMenu = true; } else { s_wantCreateMenu = true; } }`.


## F457 [UNVERIFIED] high (reporter: high) soundness — tools/mforce_ui/main.cpp:14427

**Fit (F key / status-bar Fit) queries imnodes positions for nodes hidden in collapsed groups, hitting imnodes' assert / Pool[-1] read**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `14430-14431: `for (auto& node : s_nodes) { ImVec2 pos = ImNodes::GetNodeGridSpacePos(node.id);` — iterates ALL s_nodes, not only those drawn this frame. imnodes.cpp:2786-2788: `const int node_idx = ObjectPoolFind(editor.Nodes, node_id); IM_ASSERT(node_idx != -1); ImNodeData& node = editor.Nodes.Pool[node_idx];` and imnodes_internal.h:403-413 frees the pool slot of any node not submitted (`nodes.IdMap.SetInt(id, -1); nodes.FreeList.push_back(i);`). The file already knows this: 13974-13977 `// Returning from hiding: imnodes forgot this node's origin (pool entry destroyed)`. Nodes inside a collapsed group are not passed to draw_node (13972-13981), so after one hidden frame their pool entry is gone.`

Why: With any collapsed group present, pressing F or clicking Fit asserts in a Debug build and reads editor.Nodes.Pool[-1] in Release (IM_ASSERT compiles out), feeding garbage into the pan computation. The 14151-14153 fallback `: ImNodes::GetNodeGridSpacePos(twin->id)` for a twin with !gridPosKnown has the same hazard. The fit also hardcodes 200x100 node extents (14433-14434) instead of ImNodes::GetNodeDimensions and duplicates the bounds pass that frame_level (13916-13934) already does correctly with the visible_at_path && gridPosKnown filter.

Recommendation: Make fitAllNodes a sibling of frame_level: iterate only `visible_at_path(n.label) && n.gridPosKnown` nodes using the app-tracked n.gridPos (plus visible groups' g.pos), and use ImNodes::GetNodeDimensions for extents of nodes drawn this frame. Remove the GetNodeGridSpacePos fallback at 14153 (gridPosKnown is false exactly when imnodes has nothing to return). Better: one `level_bounds()` helper feeding both frame_level and fit.


## F458 [UNVERIFIED] high (reporter: high) soundness — tools/mforce_ui/main.cpp:14498

**Delete-key node deletion never rewires DSP: consumers keep the deleted node's ValueSource and continuous play still hears it**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `14498-14501: `if (numNodes > 0) { ... for (int nid : sel) delete_node(nid); }` with no update_all_dsp afterwards. delete_node (1114-1171) only calls stop_streams(), mark_graph_dirty(), erases s_links/s_nodes entries and fixes group/dyn-pin bookkeeping — it never touches the consumer's wired param. The reset lives only in update_node_dsp_unlocked 938-944: `// First pass: wire all pins to their ConstantSource defaults ... node.wire_pin(pin.name, pin.constantSrc);`. Compare clipboard_cut 10541-10544: `for (int id : sel) delete_node(id); ... update_all_dsp();`. Context-menu Delete 11566-11569 (`if (ImGui::MenuItem("Delete")) { delete_node(s_contextNodeId); g_selectedNodeId = -1; }`) has the same gap. play_continuous reads the live wiring: 4873 `ValueSource* src = find_output_source();` and 4907-4908 `GraphNode* src = find_source_node(pin.id); if (src && src->dspSource) return src->dspSource.get();`.`

Why: The consumer's set_param holds its own shared_ptr to the deleted node's DSP object, so after Delete the UI shows the node gone but the DSP graph is unchanged: pressing Play (continuous, either graph mode) still renders the deleted LFO/filter/etc. until some unrelated edit (link add/remove, paste, load) happens to call update_all_dsp. Generate is unaffected only because generate_unified re-serializes from s_nodes/s_links (4351-4352). Two of the three user-facing delete paths are inconsistent with the third.

Recommendation: Call update_all_dsp() inside delete_node (after the s_nodes erase) so every caller is covered, and drop the now-redundant call from clipboard_cut. Alternatively have the Delete-key handler and the context-menu item share one `delete_selection()` that ends with update_all_dsp().


## F459 [UNVERIFIED] low (reporter: low) duplication — tools/mforce_ui/main.cpp:14580

**Three different hand-rolled basename extractions of s_currentFilePath in the same function**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `14582-14585: `const char* slash = strrchr(s_currentFilePath.c_str(), '/'); const char* bs = strrchr(s_currentFilePath.c_str(), '\\'); fname = (bs && (!slash || bs > slash)) ? bs + 1 : slash ? slash + 1 : s_currentFilePath.c_str();`; 13626-13630 the same strrchr dance for the window title; 13859-13860 `std::filesystem::path(s_currentFilePath).stem().string()` for the breadcrumb.`

Why: Two copies of manual separator parsing next to a std::filesystem call that already does it; <filesystem> is already included and used, so the manual versions are just surface area.

Recommendation: One `static std::string current_file_display_name()` returning `std::filesystem::path(s_currentFilePath).filename().string()` (or '(unsaved patch)'), used by title, breadcrumb and modal.


## F460 [UNVERIFIED] medium (reporter: medium) workaround-hack — tools/mforce_ui/main.cpp:14630

**Generate is a blocking render on the UI thread sequenced by a magic-int state machine so one 'Generating...' frame gets drawn first**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `14630-14636: `// Generate state machine: this frame drew "Generating..." with the waveform cleared. Now run the blocking generation. UI freezes until it completes; the frozen screen still shows "Generating...". if (s_genState == 2) { transport_generate(); s_genState = 0; }`. The phases are bare ints spread over 3640 `static int s_genState = 0;`, 8739/8748 (`s_genState >= 1` / `= 1`), 13594-13595 (`== 1` -> `= 2`), and here.`

Why: The comment itself labels this a freeze. While transport_generate runs, voice_gc (14568), audio_watchdog (14569) and pump_midi (14570) do not run, the window cannot repaint or close, and Windows will flag it 'Not Responding' for renders longer than ~5 s (phrase/passage renders with kVoiceTailSec headroom routinely are). Magic ints 0/1/2 with cross-file-section meaning are a readability hazard.

Recommendation: Run generate_unified on a worker std::thread (it already builds its own InstrumentPatch from a serialized copy, 4351-4352, so it does not touch live graph state) and poll a std::future/atomic each frame to swap the result buffers in; replace s_genState with an `enum class GenPhase { Idle, Requested, Drawn, Running }`.


## F461 [UNVERIFIED] medium (reporter: medium) soundness — tools/mforce_ui/main.cpp:14649

**Exception exit path skips shutdown_audio/shutdown_midi, so static teardown destroys g_voices while the RtAudio callback thread may still run**

Reporters: tools-ui-chunk7-L13844-14658

Evidence: `14649-14657: `} catch (const std::exception& e) { ... crash_log_write(buf); return 1; } catch (...) { crash_log_write(...); return 1; }` — no shutdown_audio()/shutdown_midi() (those are only on the normal path at 14639-14640). Static definition order: 3647 `static std::unique_ptr<RtAudio> g_audio;` precedes 3741 `static Voice g_voices[MAX_VOICES];` (Voice holds `std::shared_ptr<mforce::PerformSource> performSource;` and `std::vector<std::shared_ptr<mforce::ValueSource>> advanceList;`, 3726-3729), so at exit g_voices (and g_streamChannels, the graph in s_nodes) are destroyed BEFORE g_audio's destructor stops the stream.`

Why: Any std::exception escaping the frame loop (nlohmann::json throws on a malformed patch opened from the menu are the obvious source; several load paths are wrapped but not all UI handlers are) returns from main with the audio thread still inside audio_callback reading g_voices/g_streamSource. The static-destruction window is short but real, and it is precisely the crash-on-crash case the crash log exists to diagnose. Verified: definition order and the absence of shutdown calls in the catch; not verified: whether the callback dereferences a voice during the window on a given run.

Recommendation: Call shutdown_midi(); shutdown_audio(); (both already safe to call unconditionally) at the top of both catch blocks before crash_log_write, or hold the RtAudio/RtMidi objects in a RAII guard declared inside main's try so unwinding stops the stream before any static is torn down.


## F462 [UNVERIFIED] medium (reporter: medium) duplication — tools/ppl_to_json/CMakeLists.txt:4

**Tools hardcode engine include paths (one of them unused) instead of consuming targets; nlohmann/json has no target of its own**

Reporters: tools-misc-build

Evidence: `target_include_directories(ppl_to_json PRIVATE ${CMAKE_SOURCE_DIR}/engine/include ${CMAKE_SOURCE_DIR}/engine/third_party) -- main.cpp includes only <nlohmann/json.hpp> and std headers, so engine/include is dead; durn_converter/CMakeLists.txt:4 repeats the same hardcoding; mforce_ui/CMakeLists.txt:1-2 'set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/engine/third_party/imgui)'; engine/CMakeLists.txt:19 exports the entire third_party directory PUBLIC.`

Why: Include paths spelled per tool are the second form of build-logic duplication; exporting all of engine/third_party publicly means every consumer can silently include rtaudio/glfw/imgui headers by directory name, and a relocation of the vendored tree touches four files.

Recommendation: add_library(nlohmann_json INTERFACE) with target_include_directories(nlohmann_json INTERFACE engine/third_party) and link it from mforce_engine (PUBLIC), ppl_to_json and durn_converter; likewise an imgui STATIC target holding the five imgui .cpp files and its include dirs so mforce_ui just links it. Drop the unused engine/include from ppl_to_json.


## F463 [UNVERIFIED] low (reporter: low) build — tools/ppl_to_json/CMakeLists.txt:13

**Two tools override the project's C++20 standard down to C++17 for no stated reason**

Reporters: tools-misc-build

Evidence: `set_target_properties(ppl_to_json PROPERTIES CXX_STANDARD 17)  and tools/durn_converter/CMakeLists.txt:10 'set_target_properties(durn_converter PROPERTIES CXX_STANDARD 17)' versus root CMakeLists.txt:4 'set(CMAKE_CXX_STANDARD 20)'.`

Why: Standard drift per target blocks sharing any C++20 engine header or helper with these tools and blocks a REUSE_FROM PCH (compile options must match). Nothing in either tool needs 17 specifically.

Recommendation: Delete both set_target_properties lines; the project default applies.


## F464 [UNVERIFIED] low (reporter: low) elegance — tools/ppl_to_json/main.cpp:74

**Twelve copies of the ostringstream-then-throw error pattern; a [[noreturn]] helper collapses ~45 lines**

Reporters: tools-misc-build

Evidence: `std::ostringstream err;
err << "line " << lineNum << ": expected uppercase motif slot, got '" << c << "' at position " << i << ...;
throw std::runtime_error(err.str());  -- repeated at 119-122, 132-135, 141-144, 185-188, 197-200, 209-212, 235-238, 247-249, 256-260, 266-268.`

Why: A quarter of the parser is error-message plumbing; the repetition hides the grammar the file is meant to express.

Recommendation: [[noreturn]] static void fail(int lineNum, const std::string& msg) { throw std::runtime_error("line " + std::to_string(lineNum) + ": " + msg); } and call it with std::format (C++20, which the project standard already is - see the CXX_STANDARD 17 override finding).


## F465 [UNVERIFIED] low (reporter: low) soundness — tools/ppl_to_json/main.cpp:270

**Input validation gaps: stoi accepts trailing garbage, filesystem exceptions in main/convert_dir are uncaught, default mode is cwd-relative**

Reporters: tools-misc-build

Evidence: `out["bars"] = std::stoi(barsStr);  (also :137, :202) - '4x' parses as 4; convert_dir:376 'for (const auto& ent : fs::directory_iterator(inDir))' and main:399 'if (fs::is_directory(in))' can throw std::filesystem_error outside any try, terminating with no message; main:394 'return convert_dir("lib/ppl", "lib/json");' only works from the repo root.`

Why: A developer tool, so the consequence is a confusing failure rather than bad data - but the parser is otherwise careful about diagnostics and these three gaps undercut that.

Recommendation: Use std::from_chars and require full consumption for every integer; wrap main's body in one try/catch that prints e.what(); resolve lib/ppl relative to the executable or require explicit args.


## F466 [UNVERIFIED] low (reporter: low) soundness — tools/ppl_to_json/main.cpp:288

**Standard functions used without their headers (compiles only via MSVC transitive includes)**

Reporters: tools-misc-build

Evidence: `ppl_to_json/main.cpp:288 'size_t k = std::strlen(key);' with no #include <cstring> (includes at 11-19 are json, filesystem, fstream, iostream, sstream, string, vector, cctype, stdexcept); tools/stk_ref/mesh2d_ref.cpp:295 'peak = std::max(peak, std::fabs(s));' with no #include <algorithm> (includes at 33-38).`

Why: CLAUDE.md plans for cross-platform; libc++/libstdc++ do not guarantee these transitive includes, so both files are a compile error waiting for the first non-MSVC build.

Recommendation: Add #include <cstring> to ppl_to_json/main.cpp and #include <algorithm> to mesh2d_ref.cpp.


## F467 [UNVERIFIED] high (reporter: high) workaround-hack — tools/stamp_test/CMakeLists.txt:1

**The UI relink-lock is handled by a runtime stamp guard, a dedicated test tool and manual exe sweeps instead of fixing the build so the running binary is never the link output**

Reporters: tools-misc-build

Evidence: `'Headless exerciser for the mforce_ui engine-stamp guard. Deliberately links nothing ... so it stays buildable and runnable while a running mforce_ui.exe holds itself locked against relinking' ; CLAUDE.md 'sweep stale renamed UI exes (build/tools/mforce_ui/Release/mforce_ui_*.exe - rename-then-link leftovers)'; tools/stamp_test/main.cpp:16-17 still says 'Build (no CMake target needed...)' although CMakeLists.txt:49 adds one.`

Why: A whole subsystem (build_stamp.h, stamp_test, --stamp mode, session-start sweep instructions, a tlog parser) exists to detect that the link was skipped because the exe was open. That is a workaround layered on a workaround. The root cause is a one-line build problem: the link target and the executed file are the same path.

Recommendation: Add a POST_BUILD step on mforce_ui that copies $<TARGET_FILE:mforce_ui> (and its PDB) to a run directory (e.g. build/run/), and launch the UI from there; the link output is then never locked, relinks always succeed, and the stamp guard, stamp_test and the exe sweep can be retired. Until then, fix the stale comment in stamp_test/main.cpp:16-17.


## F468 [UNVERIFIED] medium (reporter: medium) duplication — tools/stamp_test/main.cpp:42

**stamp_test --deps re-implements build_stamp.h's newest_from_tlogs dependency walk line for line; header comment is stale (claims no CMake target, C++17)**

Reporters: tools-tests

Evidence: `stamp::read_tlog(uiDir + "\\mforce_ui.dir\\" + config +
                         "\\mforce_ui.tlog\\CL.read.1.tlog", files);
        stamp::read_tlog(buildDir + "\\engine\\mforce_engine.dir\\" + config +
                         "\\mforce_engine.tlog\\CL.read.1.tlog", files);`

Why: tools/mforce_ui/build_stamp.h:151-192 (newest_from_tlogs) contains the same s1/s2/s3 find_last_of path slicing (154-162 vs stamp_test 53-58), the same two tlog paths (166-169 vs 61-64), and the same ROOT/BUILD/THIRD_PARTY/seen/file_time filter (172-183 vs 66-77). The only difference is keep-all vs keep-newest. The comment at stamp_test:5-6 says the tool runs 'the same detection code ... not a copy', but the --deps path IS a copy, so a fix to the tlog path layout or the filter in build_stamp.h silently leaves --deps reporting a different dependency set than the guard uses. The header comment (16-17) says 'Build (no CMake target needed ...): cl /std:c++17' while tools/stamp_test/CMakeLists.txt:5 defines the target and the project is C++20; the usage string (25) omits --scan (handled at 33); the root-search loop returns rc silently (47) so --deps can print nothing with no message.

Recommendation: Refactor build_stamp.h to a for_each_dependency(root, exeDir, callback) visitor; newest_from_tlogs and stamp_test --deps both call it. Replace the find_last_of slicing with std::filesystem::path parent_path()/filename(). Fix the header comment and usage string; print a message when the repo root is not found.


## F469 [UNVERIFIED] low (reporter: low) build — tools/stk_ref/CMakeLists.txt:57

**foreach applying /O2 and _CRT_SECURE_NO_WARNINGS runs before four of the eight targets exist; STK sources are recompiled once per executable**

Reporters: tools-misc-build

Evidence: `foreach(tgt stk_bowed_ref stk_clarinet_ref stk_flute_ref stk_brass_ref) ... endforeach()  at line 57, while stk_saxofony_ref (64), stk_blowhole_ref (76), stk_bandedwg_ref (89), stk_mesh2d_ref (99) are defined afterwards and get neither; ${STK_ROOT}/src/Stk.cpp appears in all eight source lists (lines 10, 22, 34, 47, 66, 78, 91, 101).`

Why: The later four targets compile fopen() with C4996 warnings the first four were silenced for; /O2 is redundant in a Release config anyway (CMake already passes it), so the loop is half-effective and half-pointless. Stk.cpp compiles eight times, DelayL.cpp six, SineWave.cpp seven. Reference code, so low - but it is a real inconsistency.

Recommendation: add_library(stk_core STATIC <union of the STK sources>) with the include dir and _CRT_SECURE_NO_WARNINGS set PUBLIC on it; each ref executable becomes add_executable(x x_ref.cpp) + target_link_libraries(x stk_core). Delete the /O2 line.


## F470 [UNVERIFIED] medium (reporter: medium) duplication — tools/stk_ref/bowed_ref.cpp:31

**write_wav16_mono is copied verbatim into all eight stk_ref programs**

Reporters: arch-duplication, tools-misc-build

Evidence: `bowed_ref.cpp:31 'static bool write_wav16_mono(const std::string& path, const std::vector<double>& samples)' and mesh2d_ref.cpp:192 same body; also bandedwg_ref.cpp:28, blowhole_ref.cpp:28, brass_ref.cpp:24, clarinet_ref.cpp:26, flute_ref.cpp:23, saxofony_ref.cpp:28 (grep). Each also repeats the kSampleRate/argv outdir boilerplate (bowed_ref.cpp:24, 84-86; mesh2d_ref.cpp:42, 262-264).`

Why: Eight identical 25-line WAV writers plus eight copies of the argv/sample-rate preamble; a format or clipping fix has to be applied eight times. The engine's wav_writer.cpp cannot be linked because stk_ref is deliberately a separate project, but a shared header inside tools/stk_ref can.

Recommendation: Add tools/stk_ref/ref_common.h with write_wav16_mono, midi_hz and a parse_args(argc, argv, outdir, sampleRate) helper; include it from the eight programs.

Also reported as: stk_ref harnesses copy write_wav16_mono/midi_hz/constants eight times; wav_check.c and wav_check.cpp are the same program and neither is built


## F471 [UNVERIFIED] low (reporter: low) soundness — tools/stk_ref/mesh2d_ref.cpp:171

**MeshBig is ~300 KB of inline arrays constructed on the stack; fine under MSVC's 1 MB default but one NMAX bump from overflow**

Reporters: tools-misc-build

Evidence: `OnePole filterX_[NMAX], filterY_[NMAX]; StkFloat v_[NMAX][NMAX]; StkFloat vxp_..vym_[NMAX][NMAX]; StkFloat vxp1_..vym1_[NMAX][NMAX];  with 'const int NMAX = 64;' (52) = 9 x 64 x 64 x 8 bytes = 294,912 bytes plus 128 OnePole; constructed as locals at identity_check:244 'MeshBig big(12, 12);' and main:287 'MeshBig m(c.nx, c.ny);'.`

Why: Reference code, judged lightly: it works today, but NMAX = 96 would need ~660 KB and 128 would overflow the default stack silently in Release. The comment at 10-11 says the cap was raised precisely because 12 was too small for Chafe's plate, so a further raise is plausible.

Recommendation: Hold the state in std::vector<StkFloat> sized NX*NY (or construct MeshBig via std::make_unique) so the cap is a runtime parameter and the stack is not involved.


## F472 [UNVERIFIED] medium (reporter: medium) duplication — tools/test_figures/main.cpp:73

**PieceTemplate scaffold copied six times, three identical Result early-return ladders, eight identical integ_* wrappers, and a local total_duration() duplicating MelodicFigure::total_duration()**

Reporters: tools-tests

Evidence: `PieceTemplate tmpl;
    tmpl.keyName = "C";
    tmpl.scaleName = "Major";
    tmpl.bpm = 100.0f;
    tmpl.masterSeed = 0xABCDu;
    PieceTemplate::SectionTemplate sec;
    sec.name = "Main";`

Why: The same 30-line C-Major/bpm-100/'Main'/'melody'/C4 template is built at 73-104 (compose_locked), 154-185 (test_smoke_round_trip), 712-743 (integ_pitch_realization), 783-810 (compose_two_figure), 910-937 (compose_elaborated) and 1126-1154 (run_render), differing only in section beats and the phrase payload. The 'if (piece.parts.size() != 1) return {false,...}' extraction ladder is repeated at 110-118, 816-825 and 943-955. integ_invert..integ_stretch (636-694) are eight 5-line functions differing only in the transform call and tag. float total_duration(const MelodicFigure&) at 419-421 reimplements MelodicFigure::total_duration() (engine/include/mforce/music/figures.h:541). A change to PieceTemplate field defaults or to the Piece/Passage/Phrase shape has to be made in six places.

Recommendation: One make_single_phrase_template(PhraseTemplate, float sectionBeats) and one extract_phrase(const Piece&) -> const Phrase*; table-drive the integ_* set as {tag, std::function<MelodicFigure()>}; delete the local total_duration.


## F473 [UNVERIFIED] low (reporter: low) smell — tools/test_figures/main.cpp:773

**~300 lines pin the behavior of strategies Matt flags as probably-obsolete, including one assertion already weakened to pass**

Reporters: tools-tests

Evidence: `// RFB's shape selection (arc/run/zigzag/...) may produce slightly fewer
    // units than `count` depending on which shape is chosen; only assert
    // a non-empty figure and that invert preserves the unit count.`

Why: integ_two_figure_* (828-897) and integ_elab_* (958-1052) exercise TwoFigurePhraseStrategy and ElaboratedPhraseStrategy; test_build_approach_steps/test_settle_tail (547-598) exercise DefaultPhraseStrategy — all on the review's known-unfinished/awaiting-purge list. integ_two_figure_count_invert (836-839) no longer asserts the count it was written to assert. When the strategies are purged these tests break the build; until then they cost compile time and give false confidence in code slated for removal.

Recommendation: Record in the purge plan that tools/test_figures/main.cpp 547-598 and 773-1072 go with the strategies; keep the figure_transforms unit tests (214-496) and the Locked-figure integ tests (636-770), which test surviving code.


## F474 [UNVERIFIED] low (reporter: low) build — tools/wav_check.cpp:13

**wav_check.cpp is an orphan (not built by any CMakeLists, untouched since the 2026-03-29 port) and its header handling is wrong for anything but a canonical 44-byte 16-bit file**

Reporters: tools-tests

Evidence: `fseek(f, 44, SEEK_SET); // skip WAV header
    long nSamples = (fsize - 44) / 2;
 ...
        fread(&s, 2, 1, f);
 ...
    rms = sqrt(rms / nSamples);`

Why: Root CMakeLists.txt (43-50) adds engine, mforce_cli, mforce_ui, durn_converter, test_figures, ppl_to_json, stamp_test, engine_tests — no wav_check; no other reference exists in the repo. If it were built: it assumes a 44-byte header (any LIST/INFO chunk or 24-bit/float data is misread as samples), ignores fread's return, divides by zero on a header-only file (28), and reports interleaved sample count as 'samples' (the engine's write_wav_16le_stereo writes stereo, so it is 2x frames). Dead code in tools/ that reviewers still have to read.

Recommendation: Delete it (mforce_cli --gatecheck and the Python refmetrics already cover WAV inspection). If a C++ checker is wanted, build it from CMake, walk RIFF chunks to the 'data' chunk, read fmt, and report per-channel frames.



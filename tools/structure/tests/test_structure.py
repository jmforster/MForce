"""Unit and known-answer tests for the structure meter.
Run: python -m unittest discover -s tools/structure/tests"""
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import measure                      # noqa: E402
import report                       # noqa: E402
import rules                        # noqa: E402
from modules import ROOT, ModuleMap  # noqa: E402

SRC = """#include "mforce/core/a.h"
#include <nlohmann/json.hpp>
static int counter = 0;            // mutable
static const int kLimit = 3;       // not mutable
static float helper(float x) { return x; }
static std::pair<std::shared_ptr<ValueSource>,
                 int> wrapped_return_type(int a) { return {}; }
std::vector<int> g_list;           // mutable global
struct Foo {};
class Bar;
struct Base : Other {};
enum class Kind { A, B };
// changed 2026-09-01 for backlog 12
// Comp backlog #7
int x = const_cast<int&>(y);
"""

MAP = {
    "third_party": {"json": ["nlohmann/"]},
    "file_allowed": {"json": ["engine/include/mforce/core/a_json.h", "tools/"]},
    "modules": [
        {"name": "core", "purpose": "", "paths": ["engine/include/mforce/core/"], "may_include": []},
        {"name": "source", "purpose": "", "paths": ["engine/include/mforce/source/"], "may_include": ["core"]},
    ],
}

LIZARD_DUP = """Duplicates
===================================
Duplicate block:
--------------------------
a\\b.h:631 ~ 649
a\\b.h:686 ~ 698
^^^^^^^^^^^^^^^^^^^^^^^^^^

Total duplicate rate: 6.74%
"""


class MeasureTests(unittest.TestCase):
    def test_lines_counted_like_wc(self):
        self.assertEqual(len(measure.lines_of("a\nb\n")), 2)
        self.assertEqual(len(measure.lines_of("a\nb")), 2)
        self.assertEqual(len(measure.lines_of("a\r\nb\r\n")), 2)

    def test_includes(self):
        self.assertEqual(measure.includes(SRC), ["mforce/core/a.h", "nlohmann/json.hpp"])

    def test_mutable_vars_skip_consts_functions_and_wrapped_return_types(self):
        # counter, g_list, and x (the const_cast line defines a variable too)
        self.assertEqual(measure.file_level_mutable_vars(SRC), 3)

    def test_flags_include_hash_backlog_ids(self):
        f = measure.flag_counts(SRC)
        self.assertEqual((f["const_cast"], f["dynamic_cast"], f["changelog_comments"]), (1, 0, 2))

    def test_type_declarations_skip_forward_decls_keep_derived(self):
        self.assertEqual(measure.type_declarations(SRC), ["Foo", "Base", "Kind"])

    def test_parse_duplicates(self):
        blocks = measure.parse_duplicates(LIZARD_DUP)
        self.assertEqual(blocks, [(19, [("a/b.h", 631, 649), ("a/b.h", 686, 698)])])

    def test_function_lengths_and_token_duplicates_on_a_temp_file(self):
        d = tempfile.mkdtemp()
        body = "\n".join(f"    int v{i} = a{i} * 2 + b{i};" for i in range(14))
        src = ("void f1(int a0, int b0) {\n" + body + "\n}\n"
               "void f2(int a0, int b0) {\n" + body.replace("v", "w") + "\n}\n")
        path = os.path.join(d, "dup.cpp")
        with open(path, "w") as fh:
            fh.write(src)
        names = {name for name, _, n in measure.function_lengths(path)}
        self.assertEqual(names, {"f1", "f2"})
        blocks = measure.token_duplicates(["dup.cpp"], d)
        self.assertEqual(len(blocks), 1)
        self.assertGreaterEqual(blocks[0][0], 10)


class RulesAndReportTests(unittest.TestCase):
    def test_include_violations_and_file_allowed(self):
        m = ModuleMap(MAP)
        v = rules.include_violations("engine/include/mforce/core/x.h", "core",
                                     ["mforce/source/s.h", "nlohmann/json.hpp", "vector"], m)
        self.assertEqual([x["target"] for x in v], ["source", "json"])
        self.assertEqual(rules.include_violations("engine/include/mforce/core/a_json.h", "core",
                                                  ["nlohmann/json.hpp"], m), [])
        self.assertEqual(rules.include_violations("tools/x/main.cpp", None, ["nlohmann/json.hpp"], m), [])

    def test_owner_and_include_target(self):
        m = ModuleMap(MAP)
        self.assertEqual(m.owner("engine/include/mforce/core/x.h"), "core")
        self.assertIsNone(m.owner("tools/other.cpp"))
        self.assertEqual(m.include_target("mforce/core/x.h"), "core")
        self.assertIsNone(m.include_target("vector"))
        self.assertIsNone(m.include_target("build_stamp.h"))

    def test_snapshot_and_delta(self):
        limits = {"file_lines": 10, "function_lines": 5, "file_level_mutable_vars": 0}
        f = lambda p, n, fn, types: {"path": p, "module": "core", "lines": n, "functions": fn,
                                     "mutable_vars": 0, "flags": {"const_cast": 0, "dynamic_cast": 0,
                                     "changelog_comments": 0}, "types": types}
        old = report.snapshot([f("a.h", 12, [("f", 1, 8)], ["A"])], limits, [], [])
        new = report.snapshot([f("a.h", 9, [("f", 1, 4)], ["A", "B"]), f("b.h", 3, [], [])], limits, [], [])
        self.assertEqual((old["summary"]["files_over_limit"], old["summary"]["functions_over_limit"]), (1, 1))
        d = report.delta(old, new)
        self.assertIn("- `b.h` (3 lines)", d)
        self.assertIn("- NEW `B` in `a.h`", d)
        self.assertIn("- `a.h`: 12 -> 9 (-3)", d)
        self.assertIn("| files_over_limit | 1 | 0 | -1 |", d)
        self.assertIn("# Structure meter", report.render(new, limits))


class KnownAnswerTests(unittest.TestCase):
    """Spec 2026-10-04 §3.12: the meter must report what the audit measured."""

    def test_a_loop_written_twice_is_found(self):
        # The shape the audit found in the instrument: one ring-out loop
        # written in two functions with different variable names. The copy is
        # a fixture, so the test holds whatever the tree's own code does.
        def loop(buf, level, limit):
            return [
                f"  for (int i = base; i < {limit}; ++i) {{",
                f"    if (i >= renderSamples && {level} < kRingFloor) break;",
                f"    render_one(voice, {buf}, startFrame, gain);",
                f"    {level} = std::max(std::fabs({buf}[size_t(i)]), {level} * ringDecay);",
                "    rendered = i + 1;",
                "    const int fadeN = int(0.08f * float(sampleRate));",
                f"    if (i >= {limit} - fadeN && {level} >= kRingFloor)",
                f"      {buf}[size_t(i)] *= float({limit} - i) / float(fadeN);",
                "  }",
                "  int checkStart = std::max(0, rendered - sampleRate / 1000);",
                "  float tailPeak = 0.0f;",
                "  for (int i = checkStart; i < rendered; ++i)",
                f"    tailPeak = std::max(tailPeak, std::fabs({buf}[size_t(i)]));",
                "  if (tailPeak > 1e-4f) report(tailPeak);",
            ]
        src = (["void play_fresh() {"] + loop("buf", "ringEnv", "maxSamples") + ["}", "",
               "void finish_line() {"] + loop("lineBuf", "follower", "cap") + ["}", ""])
        with tempfile.TemporaryDirectory() as root:
            (Path(root) / "twice.h").write_text("\n".join(src), encoding="utf-8")
            blocks = measure.token_duplicates(["twice.h"], root)
        anchors = [i + 1 for i, l in enumerate(src) if "render_one(voice, " in l]
        self.assertEqual(len(anchors), 2)
        spans = [(s, e) for _, places in blocks for _, s, e in places]
        for a in anchors:
            covering = [e - s for s, e in spans if s <= a <= e]
            self.assertTrue(covering, f"no duplicate block covers line {a}")
            self.assertGreaterEqual(max(covering), 10)

    def test_a_module_tests_folder_belongs_to_tests(self):
        # The longest listed path wins: engine/render/tests/ inside engine/render/.
        m = ModuleMap.load()
        self.assertEqual(m.owner("engine/render/tests/voice_tests.cpp"), "tests")
        self.assertEqual(m.owner("engine/render/src/voice.cpp"), "render")
        self.assertEqual(m.owner("engine/testing/tests/main.cpp"), "tests")
        self.assertEqual(m.owner("engine/testing/src/check.cpp"), "testing")

    def test_every_tracked_source_is_on_a_module(self):
        import subprocess
        from check import SOURCE_GLOBS
        m = ModuleMap.load()
        paths = subprocess.check_output(["git", "-C", str(ROOT), "ls-files"] + SOURCE_GLOBS, text=True).split()
        self.assertTrue(paths)
        self.assertEqual([p for p in paths if m.owner(p) is None], [])


MODULE_KEYWORDS = ("JSON", "SOURCES", "USES", "LIBS")


def call_list(words, keyword):
    """The words after `keyword` in a call, up to the next keyword."""
    if keyword not in words:
        return []
    rest = words[words.index(keyword) + 1:]
    ends = [i for i, w in enumerate(rest) if w in MODULE_KEYWORDS]
    return rest[:ends[0]] if ends else rest


def module_call(cmake_text):
    """(name, words) of the mforce_module call in a CMakeLists.txt, or None."""
    call = re.search(r"mforce_module\(\s*(\w+)(.*?)\)", cmake_text, re.S)
    return (call.group(1), call.group(2).split()) if call else None


def module_uses(cmake_text):
    """(name, USES list) of the mforce_module call in a module's CMakeLists.txt,
    or None when the file makes no module."""
    call = module_call(cmake_text)
    return (call[0], call_list(call[1], "USES")) if call else None


def module_libs(cmake_text):
    """The LIBS list of the mforce_module call in a CMakeLists.txt."""
    call = module_call(cmake_text)
    return call_list(call[1], "LIBS") if call else []


def program_links(cmake_text, target):
    """What the program `target` links, in the map's names: mforce_<m> is the
    module m, mforce_lib_<family> or mforce_lib_<family>_<part> the family."""
    call = re.search(r"target_link_libraries\(\s*" + target + r"\b(.*?)\)", cmake_text, re.S)
    if not call:
        return []
    names = []
    for word in call.group(1).split():
        if word.startswith("mforce_lib_"):
            names.append(word[len("mforce_lib_"):].split("_")[0])
        elif word.startswith("mforce_"):
            names.append(word[len("mforce_"):])
    return sorted(set(names))


class ModuleTargetTests(unittest.TestCase):
    """The compiler enforces each module's USES and LIBS (engine/<m>/ and
    editor/<m>/CMakeLists.txt); the meter enforces its may_include
    (modules.json). The two say the same."""

    def test_module_uses_reads_the_call(self):
        text = "mforce_module(render\n  SOURCES src/a.cpp\n  USES    core source)\n"
        self.assertEqual(module_uses(text), ("render", ["core", "source"]))
        self.assertEqual(module_uses("mforce_module(util)\n"), ("util", []))
        self.assertIsNone(module_uses("add_executable(x main.cpp)\n"))

    def test_a_list_stops_at_the_next_keyword(self):
        text = "mforce_module(ui_views\n  SOURCES src/a.cpp\n  USES    ui_model core\n  LIBS    imgui)\n"
        self.assertEqual(module_uses(text), ("ui_views", ["ui_model", "core"]))
        self.assertEqual(module_libs(text), ["imgui"])
        text = "mforce_module(patch JSON\n  USES core\n  SOURCES src/a.cpp)\n"
        self.assertEqual(module_uses(text), ("patch", ["core"]))
        self.assertEqual(module_libs(text), [])

    def test_program_links_reads_modules_and_families(self):
        text = "target_link_libraries(mforce_editor PRIVATE\n  mforce_ui_views mforce_lib_imgui_backends mforce_lib_glfw)\n"
        self.assertEqual(program_links(text, "mforce_editor"), ["glfw", "imgui", "ui_views"])

    def test_uses_equals_may_include(self):
        m = ModuleMap.load()
        made = {}
        for top in ("engine", "editor"):
            for cmake in sorted((ROOT / top).glob("*/CMakeLists.txt")):
                text = cmake.read_text(encoding="utf-8")
                found = module_uses(text)
                if found:
                    made[found[0]] = found[1] + module_libs(text)
        app = ROOT / "editor" / "ui_app" / "CMakeLists.txt"
        if app.exists():
            made["ui_app"] = program_links(app.read_text(encoding="utf-8"), "mforce_editor")
        in_map = {mod["name"] for mod in m.modules
                  if f"engine/{mod['name']}/" in mod["paths"] or f"editor/{mod['name']}/" in mod["paths"]}
        self.assertEqual(sorted(made), sorted(in_map), "modules made by CMake vs modules in the map")
        for name, uses in sorted(made.items()):
            may = m.by_name[name]["may_include"]
            self.assertEqual(sorted(uses), sorted(may),
                             f"module {name}: USES and LIBS {uses} in its CMakeLists.txt, may_include {may} in modules.json")


class EditorTests(unittest.TestCase):
    """The editor's limits hold from its first file: every source under
    editor/ is on a module (a file on none is not checked for includes), and
    the baseline allows nothing under editor/."""

    def test_every_editor_source_is_on_a_module(self):
        from check import SOURCE_EXTENSIONS
        m = ModuleMap.load()
        paths = [p.relative_to(ROOT).as_posix() for p in sorted((ROOT / "editor").rglob("*"))
                 if p.is_file() and p.suffix.lstrip(".") in SOURCE_EXTENSIONS]
        self.assertTrue(paths)
        self.assertEqual([p for p in paths if m.owner(p) is None], [])

    def test_the_baseline_names_no_editor_file(self):
        import json
        entries = json.loads((HERE.parent / "baseline.json").read_text(encoding="utf-8"))
        self.assertTrue(entries)
        self.assertEqual([e for e in entries if e["file"].replace("\\", "/").startswith("editor/")], [])


if __name__ == "__main__":
    unittest.main()

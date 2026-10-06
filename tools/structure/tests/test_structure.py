"""Unit and known-answer tests for the structure meter.
Run: python -m unittest discover -s tools/structure/tests"""
import os
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
        self.assertEqual(measure.file_level_mutable_vars(SRC), 2)

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

    def test_ui_main_function_lengths(self):
        lengths = {name: n for name, _, n in measure.function_lengths(str(ROOT / "tools/mforce_ui/main.cpp"))}
        self.assertEqual(lengths["main"], 1711)
        self.assertEqual(lengths["draw_properties_panel"], 1035)

    def test_instrument_ring_out_duplicate_is_found(self):
        blocks = measure.token_duplicates(["engine/include/mforce/render/instrument.h"], ROOT)
        spans = [(s, e) for _, places in blocks for _, s, e in places]
        self.assertIn((631, 649), spans)
        self.assertIn((686, 698), spans)

    def test_every_tracked_source_is_on_a_module(self):
        import subprocess
        m = ModuleMap.load()
        globs = ["engine/include/*.h", "engine/src/*.cpp", "tools/*.cpp", "tools/*.h"]
        paths = subprocess.check_output(["git", "-C", str(ROOT), "ls-files"] + globs, text=True).split()
        self.assertEqual([p for p in paths if m.owner(p) is None], [])


if __name__ == "__main__":
    unittest.main()

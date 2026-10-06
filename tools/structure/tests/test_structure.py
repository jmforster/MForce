"""Unit tests for the structure meter. Run: python -m unittest discover -s tools/structure/tests"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import measure                      # noqa: E402
from modules import ModuleMap       # noqa: E402

SRC = """#include "mforce/core/a.h"
#include <nlohmann/json.hpp>
static int counter = 0;            // mutable
static const int kLimit = 3;       // not mutable
static float helper(float x) { return x; }
std::vector<int> g_list;           // mutable global
struct Foo {};
class Bar;
enum class Kind { A, B };
// changed 2026-09-01 for backlog 12
int x = const_cast<int&>(y);
"""

MAP = {
    "third_party": {"json": ["nlohmann/"]},
    "modules": [
        {"name": "core", "purpose": "", "paths": ["engine/include/mforce/core/"], "may_include": []},
        {"name": "source", "purpose": "", "paths": ["engine/include/mforce/source/"], "may_include": ["core"]},
    ],
}


class MeasureTests(unittest.TestCase):
    def test_includes(self):
        self.assertEqual(measure.includes(SRC), ["mforce/core/a.h", "nlohmann/json.hpp"])

    def test_mutable_vars_counts_static_and_globals_not_consts_or_functions(self):
        self.assertEqual(measure.file_level_mutable_vars(SRC), 2)

    def test_flags(self):
        f = measure.flagged_lines(SRC)
        self.assertEqual((f["const_cast"], f["dynamic_cast"], f["changelog_comments"]), (1, 0, 1))

    def test_type_declarations_skip_forward_decls(self):
        self.assertEqual(measure.type_declarations(SRC), ["Foo", "Kind"])

    def test_token_duplicates_catch_renamed_copies(self):
        d = tempfile.mkdtemp()
        body = "\n".join(f"    int v{i} = a{i} * 2 + b{i};" for i in range(14))
        src = ("void f1(int a0, int b0) {\n" + body + "\n}\n"
               "void f2(int a0, int b0) {\n" + body.replace("v", "w") + "\n}\n")
        with open(os.path.join(d, "dup.cpp"), "w") as fh:
            fh.write(src)
        blocks = measure.token_duplicates(["dup.cpp"], d)
        self.assertEqual(len(blocks), 1)
        self.assertEqual({p for p, _, _ in blocks[0][1]}, {"dup.cpp"})
        self.assertGreaterEqual(blocks[0][0], 10)


class ModuleMapTests(unittest.TestCase):
    def test_owner_and_may_include(self):
        m = ModuleMap(MAP)
        self.assertEqual(m.owner("engine/include/mforce/core/x.h"), "core")
        self.assertIsNone(m.owner("tools/other.cpp"))
        self.assertEqual(m.include_target("mforce/core/x.h"), "core")
        self.assertEqual(m.include_target("nlohmann/json.hpp"), "json")
        self.assertIsNone(m.include_target("vector"))
        self.assertTrue(m.may_include("source", "core"))
        self.assertFalse(m.may_include("core", "json"))
        self.assertTrue(m.may_include("core", "core"))


if __name__ == "__main__":
    unittest.main()

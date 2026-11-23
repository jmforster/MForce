"""The gate's ratchet: baseline.violations, compare and tighten over text
fixtures, and the long-path include rule.
Run: python -m unittest discover -s tools/structure/tests"""
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import baseline                     # noqa: E402
import measure                      # noqa: E402
import rules                        # noqa: E402
from modules import ModuleMap       # noqa: E402

LIMITS = {"file_lines": 600, "function_lines": 80, "file_level_mutable_vars": 0}
MAP = {
    "third_party": {"json": ["nlohmann/"]},
    "file_allowed": {"json": ["tools/"]},
    "modules": [
        {"name": "core", "purpose": "", "paths": ["engine/core/"], "may_include": []},
        {"name": "render", "purpose": "", "paths": ["engine/render/"], "may_include": ["core"]},
        {"name": "tests", "purpose": "", "paths": ["engine/render/tests/"], "may_include": ["core", "render"]},
    ],
}


def measured(path, lines=100, functions=(), mutable=0, const_cast=0, dynamic_cast=0, changelog=0,
             module="render"):
    """One file's measurements as the meter records them."""
    return {"path": path, "lines": lines, "module": module, "mutable_vars": mutable,
            "signatures": [(sig.split("(")[0], sig, 1, n) for sig, n in functions],
            "flags": {"const_cast": const_cast, "dynamic_cast": dynamic_cast,
                      "changelog_comments": changelog}}


def gate(files, base, incs=()):
    return baseline.compare(baseline.violations(files, list(incs), LIMITS), base, LIMITS)


BIG = "engine/render/src/big.cpp"
BASE = baseline.violations([measured(BIG, lines=900, functions=[("big_fn()", 300)])], [], LIMITS)


class RatchetTests(unittest.TestCase):
    def test_the_recorded_tree_passes(self):
        self.assertEqual(gate([measured(BIG, lines=900, functions=[("big_fn()", 300)])], BASE), [])

    def test_a_line_added_to_a_baselined_file_fails(self):
        f = gate([measured(BIG, lines=901, functions=[("big_fn()", 300)])], BASE)
        self.assertEqual(f, [(BIG, "file_lines", "", 901, 900)])

    def test_a_new_file_over_a_limit_fails(self):
        f = gate([measured(BIG, lines=900, functions=[("big_fn()", 300)]),
                  measured("engine/render/src/new.cpp", lines=700)], BASE)
        self.assertEqual(f, [("engine/render/src/new.cpp", "file_lines", "", 700, 600)])

    def test_a_new_long_function_beside_a_baselined_one_fails(self):
        f = gate([measured(BIG, lines=900, functions=[("big_fn()", 300), ("other(int x)", 90)])], BASE)
        self.assertEqual(f, [(BIG, "function_lines", "other(int x)", 90, 80)])

    def test_two_functions_of_one_signature_are_told_apart(self):
        keys = baseline.function_keys([("f", "lambda()", 1, 90), ("f", "lambda()", 9, 95)])
        self.assertEqual(keys, [("lambda()", 90), ("lambda() #2", 95)])

    def test_a_file_that_improved_passes(self):
        self.assertEqual(gate([measured(BIG, lines=700, functions=[("big_fn()", 200)])], BASE), [])

    def test_tighten_lowers_an_entry_and_drops_a_cured_one(self):
        now = baseline.violations([measured(BIG, lines=700, functions=[("big_fn()", 60)])], [], LIMITS)
        new = baseline.tighten(now, BASE)
        self.assertEqual([(e["rule"], e["value"]) for e in new], [("file_lines", 700)])

    def test_tighten_never_raises_or_adds(self):
        now = baseline.violations([measured(BIG, lines=950, functions=[("big_fn()", 300), ("x()", 99)])],
                                  [], LIMITS)
        new = baseline.tighten(now, BASE)
        self.assertEqual(new, BASE)

    def test_a_new_const_cast_and_changelog_comment_fail(self):
        f = gate([measured(BIG, lines=900, functions=[("big_fn()", 300)], const_cast=1, changelog=1)], BASE)
        self.assertEqual([x[1] for x in f], ["const_cast", "changelog"])

    def test_a_dynamic_cast_fails_in_the_engine_only(self):
        f = gate([measured("engine/core/a.h", dynamic_cast=1, module="core"),
                  measured("tools/x/main.cpp", dynamic_cast=1, module=None)], [])
        self.assertEqual([x[1] for x in f], ["dynamic_cast", "unmapped"])

    def test_a_new_include_edge_fails(self):
        m = ModuleMap(MAP)
        incs = rules.include_violations("engine/core/a.h", "core", ["mforce/render/voice.h"], m)
        f = gate([measured("engine/core/a.h", module="core")], [], incs)
        self.assertEqual(f, [("engine/core/a.h", "include", "mforce/render/voice.h", 1, 0)])

    def test_a_header_named_by_its_folder_is_a_violation(self):
        m = ModuleMap(MAP)
        incs = ["render/include/mforce/render/voice.h", "render/src/voice.cpp", "mforce/render/voice.h",
                "core/tests/fixtures.h"]
        v = rules.include_violations("engine/render/tests/a_tests.cpp", "tests", incs, m)
        self.assertEqual([x["include"] for x in v],
                         ["render/include/mforce/render/voice.h", "render/src/voice.cpp"])
        self.assertEqual({x["target"] for x in v}, {"long-path"})

    def test_a_new_duplicated_block_does_not_fail(self):
        body = "\n".join(f"    int v{i} = a{i} * 2 + b{i};" for i in range(14))
        src = ("void f1(int a0, int b0) {\n" + body + "\n}\n"
               "void f2(int a0, int b0) {\n" + body.replace("v", "w") + "\n}\n")
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "dup.cpp").write_text(src, encoding="utf-8")
            self.assertTrue(measure.token_duplicates(["dup.cpp"], d))   # the report sees it
            fns = measure.functions(str(Path(d) / "dup.cpp"))
        f = measured("engine/render/src/dup.cpp", lines=len(src.splitlines()))
        f["signatures"] = fns
        self.assertEqual(gate([f], []), [])                              # the gate does not

    def test_the_baseline_file_is_one_sorted_entry_per_line(self):
        text = baseline.dumps(list(reversed(BASE)))
        self.assertEqual(text.count("\n"), len(BASE) + 2)
        self.assertLess(text.index("file_lines"), text.index("function_lines"))


if __name__ == "__main__":
    unittest.main()

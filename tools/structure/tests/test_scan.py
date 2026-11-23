"""The rules that read C++ with cxx.py (file-level mutable variables, the
changelog pattern), the long-path include rule's spellings, and which files
the gate measures.
Run: python -m unittest discover -s tools/structure/tests"""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import check                        # noqa: E402
import measure                      # noqa: E402
import rules                        # noqa: E402


def counted(src):
    return [s for _, s in measure.mutable_variables(src)]


class MutableVariableTests(unittest.TestCase):
    def test_a_global_with_no_static_counts(self):
        self.assertEqual(counted("int planted_counter = 0;\n"), ["int planted_counter = 0"])

    def test_a_static_inside_a_named_namespace_counts(self):
        self.assertEqual(len(counted("namespace planted {\n  static int counter = 0;\n}\n")), 1)

    def test_a_variable_in_an_anonymous_namespace_counts(self):
        self.assertEqual(len(counted("namespace {\nstd::size_t planted_count = 0;\n}\n")), 1)

    def test_extern_c_blocks_thread_local_and_inline_variables_count(self):
        src = 'extern "C" {\nint q;\n}\nthread_local int t = 0;\ninline int iv = 0;\nstatic std::atomic<int> n{0};\n'
        self.assertEqual(len(counted(src)), 4)

    def test_function_declarations_and_definitions_do_not(self):
        src = "int f(int a);\nvoid g();\nint h(int a) {\n  static int x = 0;\n  return a + x;\n}\n"
        self.assertEqual(counted(src), [])

    def test_a_constexpr_array_whose_initialiser_spans_lines_does_not(self):
        self.assertEqual(counted("constexpr int kT[] = {\n  1,\n  2,\n};\nnamespace {\nconstexpr X kL[] = {\n  {1},\n};\n}\n"), [])

    def test_a_struct_definition_followed_by_a_semicolon_does_not(self):
        self.assertEqual(counted("struct S {\n  int a;\n  static int b;\n};\nenum class E : int { A, B };\n"), [])

    def test_a_lambda_bound_to_a_const_auto_does_not(self):
        self.assertEqual(counted("const auto f = [](int x) {\n  int y = x;\n  return y;\n};\n"), [])

    def test_inline_constexpr_and_constinit_const_do_not(self):
        self.assertEqual(counted("inline constexpr int k = 3;\nconstinit const int c = 1;\n"), [])

    def test_an_extern_declaration_does_not(self):
        self.assertEqual(counted('extern int g_x;\nextern "C" int y;\n'), [])

    def test_a_macro_call_does_not(self):
        self.assertEqual(counted("MFORCE_REGISTER(Foo);\nMFORCE_TEST(bar) {\n  int z = 0;\n}\n"), [])

    def test_a_pointer_to_const_reads_as_const(self):
        self.assertEqual(counted('static const char* p = "x";\n'), [])

    def test_comments_and_strings_are_not_read(self):
        src = '// int a = 0;\n/* int b;\n   int c; */\nconst char* s = "int d = 0;";\nconst char* r = R"(int e;)";\n'
        self.assertEqual(counted(src), [])

    def test_an_out_of_line_defaulted_or_deleted_member_does_not(self):
        src = ("Window::~Window() = default;\nWindow::Window() = default;\n"
               "Window& Window::operator=(const Window&) = default;\nWindow::Window(const Window&) = delete;\n"
               "Window& Window::operator=(Window&&) = delete;\nWindow::~Window() = delete;\n"
               "Window::Window(Window&&) noexcept = default;\n")
        self.assertEqual(counted(src), [])

    def test_a_variable_beside_a_defaulted_member_still_counts(self):
        src = "Window::~Window() = default;\nint counter = 0;\nstatic Foo g = Foo();\n"
        self.assertEqual(counted(src), ["int counter = 0", "static Foo g = Foo()"])

    def test_a_digit_separator_is_not_a_character_literal(self):
        self.assertEqual(counted("constexpr int big = 1'000'000;\nint w = 0;\n"), ["int w = 0"])


class ChangelogTests(unittest.TestCase):
    def test_dates_and_tracker_ids_in_any_comment_form(self):
        src = ("// 2026-11-23 x\n/* 2026/11/23 */\nint a; // REVIEW #4\n// CLEANUP 3\n"
               "/* backlog#2\n   BACKLOG 7 */\n// REVIEW 12\n")
        self.assertEqual(measure.changelog_lines(src), [1, 2, 3, 4, 5, 6, 7])

    def test_what_is_not_a_date_or_an_id(self):
        src = '// version 2026\n// see the review\nconst char* d = "2026-11-23";\n'
        self.assertEqual(measure.changelog_lines(src), [])


class LongPathTests(unittest.TestCase):
    NAMES = {"core", "render"}

    def test_spellings_that_reach_a_folder(self):
        for inc in ["render/include/mforce/render/voice.h", "render/include/./mforce/render/voice.h",
                    "render/include//mforce/render/voice.h", "render/Include/mforce/render/voice.h",
                    "render\\src\\voice.cpp", "./render/src/voice.cpp", "render/include/voice.h",
                    "render/src/x.h", "third_party/nlohmann/json.hpp", "Third_Party/x.h"]:
            self.assertTrue(rules.is_long_path(inc, self.NAMES), inc)

    def test_spellings_that_do_not(self):
        for inc in ["mforce/render/voice.h", "core/tests/fixtures.h", "nlohmann/json.hpp", "vector"]:
            self.assertFalse(rules.is_long_path(inc, self.NAMES), inc)


class GatedSourcesTests(unittest.TestCase):
    def test_the_gate_measures_untracked_files_the_report_does_not(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            git = ["git", "-C", d, "-c", "user.name=t", "-c", "user.email=t@t"]
            subprocess.run(git + ["init", "-q"], check=True)
            for rel in ["engine/a.cpp", "engine/b.hpp", "engine/c.cpp", "engine/third_party/x.h", "tools/d.txt"]:
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                (root / rel).write_text("int x;\n", encoding="utf-8")
            (root / ".gitignore").write_text("engine/c.cpp\n", encoding="utf-8")
            subprocess.run(git + ["add", "engine/a.cpp"], check=True)
            saved = check.ROOT
            check.ROOT = root
            try:
                self.assertEqual(check.tracked_sources(), ["engine/a.cpp"])
                self.assertEqual(check.gated_sources(), ["engine/a.cpp", "engine/b.hpp"])
            finally:
                check.ROOT = saved


if __name__ == "__main__":
    unittest.main()

"""The allocation gate wrapper's merge: a crashed child or a short count fails."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import alloc_gate  # noqa: E402


class MergeTests(unittest.TestCase):
    def test_clean_children_pass(self):
        results = [(0, "ok a.json\n2 patches: 0 allocate, 0 failed to load\n", ""),
                   (0, "1 patches: 0 allocate, 0 failed to load\n", "")]
        lines, errs, summary, code = alloc_gate.merge(results, 3)
        self.assertEqual(code, 0)
        self.assertEqual(summary, "3 patches: 0 allocate, 0 failed to load")
        self.assertEqual(lines, ["ok a.json"])
        self.assertEqual(errs, [])

    def test_an_allocation_fails(self):
        results = [(1, "ALLOC b.json: 3 allocations, 96 bytes\n2 patches: 1 allocate, 0 failed to load\n", ""),
                   (0, "1 patches: 0 allocate, 0 failed to load\n", "")]
        _, _, summary, code = alloc_gate.merge(results, 3)
        self.assertEqual(code, 1)
        self.assertEqual(summary, "3 patches: 1 allocate, 0 failed to load")

    def test_a_load_failure_fails(self):
        results = [(2, "LOAD FAIL c.json: bad\n1 patches: 0 allocate, 1 failed to load\n", "")]
        _, _, _, code = alloc_gate.merge(results, 1)
        self.assertEqual(code, 2)

    def test_a_crashed_child_fails(self):
        results = [(0, "2 patches: 0 allocate, 0 failed to load\n", ""),
                   (-1073741819, "", "")]          # 0xC0000005 as Windows reports it
        _, errs, _, code = alloc_gate.merge(results, 3)
        self.assertEqual(code, 3)
        self.assertTrue(any("crashed" in e for e in errs))

    def test_a_short_count_fails(self):
        results = [(0, "2 patches: 0 allocate, 0 failed to load\n", "")]
        _, errs, _, code = alloc_gate.merge(results, 3)
        self.assertEqual(code, 3)
        self.assertTrue(any("2 patches of 3" in e for e in errs))


if __name__ == "__main__":
    unittest.main()

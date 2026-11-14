"""The curve-table rule: knots as two columns and expression rows as four,
each written where its list stood, the absent form/a/b defaults, a label the
loader read as its default written out, the refusals (each leaving the
document unchanged), the walk over both node-container shapes and a nested
node list, idempotence."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import curve_table  # noqa: E402

EXPR = ["exprX", "exprForm", "exprA", "exprB"]


class NoContext:
    """The rule asks nothing of its context; a question fails the test."""
    def descriptors(self):
        raise AssertionError("the curve-table rule asked for descriptors")


def curve(params, nid="c"):
    node = {"type": "CurveNode", "params": params}
    if nid is not None:
        node["id"] = nid
    return node


def patch(*nodes):
    return {"sampleRate": 48000, "graph": {"nodes": list(nodes), "output": "c"}}


def apply(doc):
    notes = []
    counts = curve_table.apply(doc, notes, NoContext())
    return counts, notes


def params_of(doc, i=0):
    return doc["graph"]["nodes"][i]["params"]


class KnotTests(unittest.TestCase):
    def test_knots_become_x_and_y_where_knots_stood(self):
        doc = patch(curve({"interp": "logx", "knots": [[100.0, 0.5], [1000.0, 2.0], [5000, 3]],
                           "source": {"ref": "f"}}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"knots": 1}))
        self.assertEqual(notes, [])
        p = params_of(doc)
        self.assertEqual(list(p), ["interp", "x", "y", "source"])
        self.assertEqual(p["x"], [100.0, 1000.0, 5000])
        self.assertEqual(p["y"], [0.5, 2.0, 3])
        self.assertIs(type(p["x"][2]), int)                                     # an int stays an int
        self.assertIs(type(p["y"][0]), float)
        self.assertEqual(p["interp"], "logx")
        self.assertEqual(p["source"], {"ref": "f"})

    def test_empty_knots_are_empty_columns(self):
        doc = patch(curve({"knots": [], "source": {"ref": "f"}}))
        counts, _ = apply(doc)
        self.assertEqual(params_of(doc), {"x": [], "y": [], "source": {"ref": "f"}})
        self.assertEqual(counts, Counter({"knots": 1}))

    def test_a_node_with_knots_alone_gets_only_that_table(self):
        doc = patch(curve({"knots": [[0, 1], [1, 2]]}))
        apply(doc)
        self.assertEqual(list(params_of(doc)), ["x", "y"])


class ExpressionTests(unittest.TestCase):
    def test_rows_become_four_columns_where_expr_knots_stood(self):
        doc = patch(curve({"mode": "expressions", "knots": [],
                           "exprKnots": [{"x": 0.0, "form": "linear", "a": 1.0, "b": 0.5},
                                         {"x": 100, "form": "power", "a": 2, "b": 0.3}],
                           "interp": "loglog", "source": {"ref": "f"}}))
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter({"knots": 1, "expression rows": 1}))
        self.assertEqual(notes, [])
        p = params_of(doc)
        self.assertEqual(list(p), ["mode", "x", "y"] + EXPR + ["interp", "source"])
        self.assertEqual(p["x"], [])
        self.assertEqual(p["y"], [])
        self.assertEqual(p["exprX"], [0.0, 100])
        self.assertEqual(p["exprForm"], ["linear", "power"])
        self.assertEqual(p["exprA"], [1.0, 2])
        self.assertEqual(p["exprB"], [0.5, 0.3])
        self.assertIs(type(p["exprX"][1]), int)
        self.assertIs(type(p["exprA"][1]), int)

    def test_absent_form_a_and_b_take_the_loaders_defaults(self):
        doc = patch(curve({"exprKnots": [{"x": 5.0}, {"x": 6.0, "form": "power"},
                                         {"x": 7.0, "a": 3.0}, {"x": 8.0, "b": 4.0}]}))
        apply(doc)
        p = params_of(doc)
        self.assertEqual(list(p), EXPR)
        self.assertEqual(p["exprX"], [5.0, 6.0, 7.0, 8.0])
        self.assertEqual(p["exprForm"], ["linear", "power", "linear", "linear"])
        self.assertEqual(p["exprA"], [0.0, 0.0, 3.0, 0.0])
        self.assertEqual(p["exprB"], [0.0, 0.0, 0.0, 4.0])
        self.assertIs(type(p["exprA"][0]), float)
        self.assertIs(type(p["exprB"][0]), float)

    def test_empty_expression_rows_are_empty_columns(self):
        doc = patch(curve({"mode": "points", "knots": [[0, 1], [1, 2]], "exprKnots": []}))
        counts, _ = apply(doc)
        self.assertEqual(params_of(doc), {"mode": "points", "x": [0, 1], "y": [1, 2],
                                          "exprX": [], "exprForm": [], "exprA": [], "exprB": []})
        self.assertEqual(counts, Counter({"knots": 1, "expression rows": 1}))

    def test_a_node_with_expression_rows_alone_gets_only_that_table(self):
        doc = patch(curve({"mode": "expressions", "exprKnots": [{"x": 0, "form": "linear", "a": 1, "b": 0}]}))
        counts, _ = apply(doc)
        self.assertEqual(list(params_of(doc)), ["mode"] + EXPR)
        self.assertEqual(counts, Counter({"expression rows": 1}))


class LabelTests(unittest.TestCase):
    def test_the_labels_are_kept(self):
        for interp in ("linear", "logx", "loglog"):
            for mode in ("points", "expressions"):
                doc = patch(curve({"interp": interp, "mode": mode, "knots": [[0, 0], [1, 1]],
                                   "exprKnots": [{"x": 0, "form": "power"}, {"x": 1, "form": "linear"}]}))
                counts, notes = apply(doc)
                p = params_of(doc)
                self.assertEqual((p["interp"], p["mode"]), (interp, mode))
                self.assertEqual(p["exprForm"], ["power", "linear"])
                self.assertEqual(counts, Counter({"knots": 1, "expression rows": 1}))
                self.assertEqual(notes, [])

    def test_another_form_string_is_written_as_linear_with_a_note(self):
        for bad in ("Power", "POWER", "pow", ""):
            doc = patch(curve({"exprKnots": [{"x": 0, "form": "power"}, {"x": 1, "form": bad}]}))
            counts, notes = apply(doc)
            self.assertEqual(params_of(doc)["exprForm"], ["power", "linear"], bad)
            self.assertEqual(counts, Counter({"expression rows": 1, "label": 1}))
            self.assertEqual(len(notes), 1)
            self.assertIn("'c'", notes[0])
            self.assertIn(repr(bad), notes[0])

    def test_another_interp_string_is_written_as_linear_with_a_note(self):
        for bad in ("LogX", "log", "Linear", ""):
            doc = patch(curve({"interp": bad, "knots": [[0, 0], [1, 1]]}))
            counts, notes = apply(doc)
            self.assertEqual(params_of(doc)["interp"], "linear", bad)
            self.assertEqual(list(params_of(doc)), ["interp", "x", "y"])
            self.assertEqual(counts, Counter({"knots": 1, "label": 1}))
            self.assertEqual(len(notes), 1)
            self.assertIn(repr(bad), notes[0])

    def test_another_mode_string_is_written_as_points_with_a_note(self):
        for bad in ("Expressions", "expression", "Points", ""):
            doc = patch(curve({"mode": bad, "knots": [[0, 0], [1, 1]]}))
            counts, notes = apply(doc)
            self.assertEqual(params_of(doc)["mode"], "points", bad)
            self.assertEqual(counts, Counter({"knots": 1, "label": 1}))
            self.assertEqual(len(notes), 1)
            self.assertIn(repr(bad), notes[0])

    def test_absent_interp_and_mode_stay_absent(self):
        doc = patch(curve({"knots": [[0, 0], [1, 1]]}))
        apply(doc)
        self.assertNotIn("interp", params_of(doc))
        self.assertNotIn("mode", params_of(doc))

    def test_a_column_form_node_is_untouched(self):
        # No old list: the rule leaves it, its labels included (the loader
        # that reads columns matches labels in any case).
        doc = patch(curve({"interp": "LogX", "mode": "Points", "x": [0, 1], "y": [0, 1]}))
        before = copy.deepcopy(doc)
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter())
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])


class RefusalTests(unittest.TestCase):
    def refused(self, params, needles, nid="c"):
        doc = patch(curve(params, nid), curve({"knots": [[0, 0], [1, 1]]}, "fine"))
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            curve_table.apply(doc, notes, NoContext())
        msg = str(cm.exception)
        for needle in needles:
            self.assertIn(needle, msg)
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])
        return msg

    def test_knots_not_a_list(self):
        for bad in ({"x": [0]}, "steep", 3, None):
            self.refused({"knots": bad}, ["'c'", "knots"])

    def test_a_knot_that_is_not_two_numbers(self):
        for bad in ([1], [1, 2, 3], [1, "2"], [True, 1], [1, None], (1, 2), {"x": 1, "y": 2}, 5, None):
            self.refused({"knots": [[0, 0], bad]}, ["'c'", "knot"])

    def test_expr_knots_not_a_list(self):
        for bad in ({"x": 0}, "linear", 0, None):
            self.refused({"exprKnots": bad}, ["'c'", "exprKnots"])

    def test_an_expression_row_that_is_not_an_object(self):
        for bad in ([0, "linear", 1, 0], 0, None, "x"):
            self.refused({"exprKnots": [bad]}, ["'c'", "expression row"])

    def test_an_expression_row_without_x(self):
        self.refused({"exprKnots": [{"form": "linear", "a": 1, "b": 0}]}, ["'c'", "x"])

    def test_an_expression_row_with_another_key(self):
        self.refused({"exprKnots": [{"x": 0, "form": "linear", "c": 1}]}, ["'c'", "'c'"])
        self.refused({"exprKnots": [{"x": 0, "y": 1}]}, ["'c'", "y"])

    def test_a_non_number_in_an_expression_row(self):
        for key in ("x", "a", "b"):
            for bad in ("1", None, True, [1]):
                row = {"x": 0, "form": "linear", "a": 1, "b": 0}
                row[key] = bad
                self.refused({"exprKnots": [row]}, ["'c'", key])

    def test_a_form_that_is_not_a_string(self):
        for bad in (1, None, True):
            self.refused({"exprKnots": [{"x": 0, "form": bad}]}, ["'c'", "form"])

    def test_interp_or_mode_that_is_not_a_string(self):
        for key in ("interp", "mode"):
            for bad in (1, None, True, {"ref": "x"}):
                self.refused({"knots": [[0, 0], [1, 1]], key: bad}, ["'c'", key])

    def test_a_column_key_beside_an_old_list(self):
        for old in ({"knots": [[0, 0], [1, 1]]}, {"exprKnots": [{"x": 0}]}):
            for col in ["x", "y"] + EXPR:
                params = dict(old)
                params[col] = []
                self.refused(params, ["'c'", col])

    def test_a_node_with_no_id_is_named(self):
        self.refused({"knots": "x"}, ["<no id>"], nid=None)

    def test_a_refusal_anywhere_leaves_every_node_unchanged(self):
        doc = patch(curve({"interp": "LOGX", "knots": [[0, 0], [1, 1]]}, "first"),
                    curve({"exprKnots": [{"x": 0, "z": 1}]}, "bad"))
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            curve_table.apply(doc, notes, NoContext())
        self.assertIn("'bad'", str(cm.exception))
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])


class WalkTests(unittest.TestCase):
    def test_top_level_nodes(self):
        doc = {"nodes": [curve({"knots": [[0, 0], [1, 1]]})], "output": "c"}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"knots": 1}))
        self.assertEqual(doc["nodes"][0]["params"], {"x": [0, 1], "y": [0, 1]})

    def test_a_document_with_no_node_list_is_untouched(self):
        for doc in ({"sampleRate": 48000}, {"graph": {"output": "c"}}):
            before = copy.deepcopy(doc)
            self.assertEqual(apply(doc)[0], Counter())
            self.assertEqual(doc, before)

    def test_a_nested_node_list_inside_a_node(self):
        inner = curve({"knots": [[0, 0], [1, 1]]}, "inner")
        doc = patch({"id": "host", "type": "SomeHost", "params": {"template": {"nodes": [inner]}}})
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"knots": 1}))
        self.assertEqual(inner["params"], {"x": [0, 1], "y": [0, 1]})

    def test_other_types_are_untouched(self):
        doc = patch({"id": "s", "type": "SineSource", "params": {"knots": [[0, 0]], "interp": "x"}})
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc)[0], Counter())
        self.assertEqual(doc, before)


class IdempotenceTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = patch(curve({"interp": "LogX", "mode": "Expressions", "knots": [],
                           "exprKnots": [{"x": 0, "form": "Power"}, {"x": 1}]}),
                    curve({"knots": [[0, 0], [1, 1]]}, "c2"))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"knots": 2, "expression rows": 1, "label": 3}))
        once = copy.deepcopy(doc)
        counts, notes = apply(doc)
        self.assertEqual(counts, Counter())
        self.assertEqual(notes, [])
        self.assertEqual(doc, once)


if __name__ == "__main__":
    unittest.main()

"""The expand-rule rule: an inline expandRule on the three Partials hosts as
an ExpandRule node inserted immediately before its host, all twelve values
written (the loader's default where absent), the host's expandRule its ref;
the ref form left alone; the refusals (each leaving the document unchanged);
the walk over both node-container shapes and a nested node list; other types
untouched; idempotence."""
import copy
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from convert_rules import expand_rule_node  # noqa: E402

HOSTS = ("FullPartials", "SequencePartials", "ExplicitPartials")
KEYS = ["count", "recurse", "spacing1", "spacing2", "dt1", "dt2",
        "loPct1", "loPct2", "power1", "power2", "po1", "po2"]
DEFAULTS = {"count": 2, "recurse": 0, "spacing1": 0.5, "spacing2": 0.5, "dt1": 0.01, "dt2": 0.01,
            "loPct1": 0.1, "loPct2": 0.1, "power1": 1.0, "power2": 1.0, "po1": 0.0, "po2": 0.0}


class NoContext:
    """The rule asks nothing of its context; a question fails the test."""
    def descriptors(self):
        raise AssertionError("the expand-rule rule asked for descriptors")


def host(ntype, params, nid="p"):
    if nid is None:
        return {"type": ntype, "params": params}
    return {"id": nid, "type": ntype, "params": params}


def osc(nid="o"):
    return {"id": nid, "type": "SineSource", "params": {}}


def patch(*nodes):
    return {"sampleRate": 48000, "graph": {"nodes": list(nodes), "output": "p"}}


def apply(doc):
    notes = []
    counts = expand_rule_node.apply(doc, notes, NoContext())
    return counts, notes


class NodeTests(unittest.TestCase):
    def test_an_inline_rule_becomes_a_node_just_before_its_host(self):
        for ntype in HOSTS:
            doc = patch(osc("a"),
                        host(ntype, {"numPartials": 8,
                                     "expandRule": {"spacing1": 0.3, "count": 4, "po2": 0.25},
                                     "amplitude": 0.5}),
                        osc("b"))
            counts, notes = apply(doc)
            self.assertEqual(counts, Counter({"expandRule": 1}), ntype)
            self.assertEqual(notes, [])
            nodes = doc["graph"]["nodes"]
            self.assertEqual([n["id"] for n in nodes], ["a", "p_expand", "p", "b"])
            rule = nodes[1]
            self.assertEqual(list(rule), ["id", "type", "params"])
            self.assertEqual(rule["type"], "ExpandRule")
            self.assertEqual(list(rule["params"]), KEYS)
            expected = dict(DEFAULTS, count=4, spacing1=0.3, po2=0.25)
            self.assertEqual(rule["params"], expected)
            h = nodes[2]
            self.assertEqual(list(h["params"]), ["numPartials", "expandRule", "amplitude"])
            self.assertEqual(h["params"]["expandRule"], {"ref": "p_expand"})

    def test_an_empty_inline_rule_is_all_twelve_defaults(self):
        doc = patch(host("FullPartials", {"expandRule": {}}))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"expandRule": 1}))
        rule = doc["graph"]["nodes"][0]
        self.assertEqual(rule, {"id": "p_expand", "type": "ExpandRule", "params": DEFAULTS})
        self.assertEqual(list(rule["params"]), KEYS)
        self.assertIs(type(rule["params"]["count"]), int)
        self.assertIs(type(rule["params"]["power1"]), float)
        self.assertIs(type(rule["params"]["po1"]), float)

    def test_every_inline_value_is_carried(self):
        values = {k: float(i) + 0.5 for i, k in enumerate(KEYS)}
        values["count"], values["recurse"] = 5, 1
        doc = patch(host("ExplicitPartials", {"expandRule": dict(reversed(list(values.items())))}))
        apply(doc)
        rule = doc["graph"]["nodes"][0]
        self.assertEqual(list(rule["params"]), KEYS)
        self.assertEqual(rule["params"], values)

    def test_two_hosts_each_get_their_own_node(self):
        doc = patch(host("FullPartials", {"expandRule": {"count": 3}}, "x"), osc(),
                    host("SequencePartials", {"expandRule": {"count": 4}}, "y"))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"expandRule": 2}))
        nodes = doc["graph"]["nodes"]
        self.assertEqual([n["id"] for n in nodes], ["x_expand", "x", "o", "y_expand", "y"])
        self.assertEqual(nodes[0]["params"]["count"], 3)
        self.assertEqual(nodes[3]["params"]["count"], 4)

    def test_the_ref_form_is_left_alone(self):
        doc = patch({"id": "r", "type": "ExpandRule", "params": {"count": 3}},
                    host("FullPartials", {"expandRule": {"ref": "r"}}))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)

    def test_a_host_without_an_expand_rule_is_untouched(self):
        doc = patch(host("FullPartials", {"numPartials": 4}), {"id": "q", "type": "FullPartials"})
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class RefusalTests(unittest.TestCase):
    def refused(self, doc, needles):
        before = copy.deepcopy(doc)
        notes = []
        with self.assertRaises(ValueError) as cm:
            expand_rule_node.apply(doc, notes, NoContext())
        msg = str(cm.exception)
        for needle in needles:
            self.assertIn(needle, msg)
        self.assertEqual(doc, before)
        self.assertEqual(notes, [])
        return msg

    def with_fine(self, bad_host):
        return patch(host("FullPartials", {"expandRule": {"count": 3}}, "fine"), bad_host)

    def test_an_inline_key_outside_the_twelve(self):
        self.refused(self.with_fine(host("FullPartials", {"expandRule": {"count": 3, "spacing": 0.2}})),
                     ["'p'", "FullPartials", "'spacing'"])

    def test_a_value_that_is_not_a_number(self):
        for key in KEYS:
            for bad in ("1", None, True, [1], {"ref": "x"}):
                self.refused(self.with_fine(host("SequencePartials", {"expandRule": {key: bad}})),
                             ["'p'", "SequencePartials", key])

    def test_a_host_with_no_id(self):
        for nid_less in ({"type": "FullPartials", "params": {"expandRule": {}}},
                         {"id": None, "type": "FullPartials", "params": {"expandRule": {}}},
                         {"id": 7, "type": "FullPartials", "params": {"expandRule": {}}},
                         {"id": "", "type": "FullPartials", "params": {"expandRule": {}}}):
            self.refused(self.with_fine(nid_less), ["FullPartials", "id"])

    def test_an_id_already_in_the_document(self):
        self.refused(patch(osc("p_expand"), host("FullPartials", {"expandRule": {}})),
                     ["'p'", "'p_expand'"])

    def test_an_id_already_in_a_nested_node_list(self):
        doc = patch({"id": "m", "type": "MultiplexSource",
                     "params": {"template": {"nodes": [osc("p_expand")]}}},
                    host("FullPartials", {"expandRule": {}}))
        self.refused(doc, ["'p_expand'"])

    def test_two_hosts_sharing_an_id(self):
        doc = patch(host("FullPartials", {"expandRule": {}}), host("ExplicitPartials", {"expandRule": {}}))
        self.refused(doc, ["'p_expand'"])

    def test_an_expand_rule_that_is_not_an_object(self):
        for bad in ("rule", 3, [1, 2], None, True):
            self.refused(self.with_fine(host("ExplicitPartials", {"expandRule": bad})),
                         ["'p'", "ExplicitPartials", "expandRule"])

    def test_a_refusal_anywhere_leaves_every_node_unchanged(self):
        doc = patch(host("FullPartials", {"expandRule": {"count": 2}}, "first"),
                    host("FullPartials", {"expandRule": {"x": 1}}, "bad"))
        msg = self.refused(doc, ["'bad'"])
        self.assertNotIn("'first'", msg)


class WalkTests(unittest.TestCase):
    def test_top_level_nodes(self):
        doc = {"nodes": [osc(), host("FullPartials", {"expandRule": {"count": 3}})], "output": "p"}
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"expandRule": 1}))
        self.assertEqual([n["id"] for n in doc["nodes"]], ["o", "p_expand", "p"])

    def test_a_nested_node_list_inside_a_node(self):
        inner = host("FullPartials", {"expandRule": {"count": 3}}, "inner")
        template = {"nodes": [osc(), inner]}
        doc = patch({"id": "m", "type": "MultiplexSource", "params": {"template": template}})
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"expandRule": 1}))
        self.assertEqual([n["id"] for n in template["nodes"]], ["o", "inner_expand", "inner"])
        self.assertEqual([n["id"] for n in doc["graph"]["nodes"]], ["m"])               # the outer list unchanged
        self.assertEqual(inner["params"]["expandRule"], {"ref": "inner_expand"})

    def test_other_types_are_untouched(self):
        doc = patch(host("Partials", {"expandRule": {"count": 3}}),
                    host("AdditiveSource", {"expandRule": "x"}, "a"),
                    host("Formant", {"expandRule": {"bad": 1}}, "f"))
        before = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, before)


class IdempotenceTests(unittest.TestCase):
    def test_a_second_pass_changes_nothing(self):
        doc = patch(host("FullPartials", {"expandRule": {"count": 3}}, "x"),
                    host("ExplicitPartials", {"expandRule": {}}, "y"))
        counts, _ = apply(doc)
        self.assertEqual(counts, Counter({"expandRule": 2}))
        once = copy.deepcopy(doc)
        self.assertEqual(apply(doc), (Counter(), []))
        self.assertEqual(doc, once)


if __name__ == "__main__":
    unittest.main()

"""tools/convert_template.py: each rule (a strategy's settings key into its
settings, periods, the rhythm pattern, realizationStrategy alone, the profile
selector's config, a key whose strategy is not the one named is dropped),
text outside the converted keys kept byte for byte, idempotence, and a file
that is not JSON left alone."""
import json
import os
import sys
import tempfile
import unittest
import pathlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import convert_template as ct  # noqa: E402


def template(passage):
    return {"keyName": "C", "parts": [{"name": "melody", "role": "melody",
                                       "passages": {"Main": passage}}]}


def convert(doc, indent=None):
    log = []
    text, count = ct.convert_text(json.dumps(doc, indent=indent), log)
    return json.loads(text), count, log, text


def passage_of(doc):
    return doc["parts"][0]["passages"]["Main"]


class SettingsKeyTests(unittest.TestCase):
    def test_passage_settings_key_becomes_settings(self):
        for key, name in ct.PASSAGE_KEYS.items():
            if key == "periods":
                continue
            out, count, log, _ = convert(template({"strategy": name, key: {"seed": 3}}))
            self.assertEqual(passage_of(out), {"strategy": {"name": name, "settings": {"seed": 3}}})
            self.assertEqual((count, log), (1, []))

    def test_phrase_settings_key_becomes_settings(self):
        for key, name in ct.PHRASE_KEYS.items():
            out, _, log, _ = convert(template({"phrases": [
                {"figures": [], "strategy": name, key: {"seed": 7}}]}))
            self.assertEqual(passage_of(out)["phrases"][0],
                             {"figures": [], "strategy": {"name": name, "settings": {"seed": 7}}})
            self.assertEqual(log, [])

    def test_periods_become_period_passage_settings(self):
        periods = [{"variant": "Parallel", "antecedent": {}, "consequent": {}}]
        out, _, _, _ = convert(template({"strategy": "period_passage", "periods": periods}))
        self.assertEqual(passage_of(out)["strategy"],
                         {"name": "period_passage", "settings": {"periods": periods}})

    def test_a_key_of_another_strategy_is_dropped_and_printed(self):
        out, _, log, _ = convert(template({"strategy": "sequence_passage",
                                           "pedalBuildupConfig": {"levels": 3}}))
        self.assertEqual(passage_of(out), {"strategy": "sequence_passage"})
        self.assertEqual(len(log), 1)
        self.assertIn("dropped pedalBuildupConfig", log[0])

    def test_a_key_with_no_strategy_named_is_dropped(self):
        out, _, log, _ = convert(template({"periods": [], "phrases": []}))
        self.assertEqual(passage_of(out), {"phrases": []})
        self.assertIn("dropped periods", log[0])


class RealizationTests(unittest.TestCase):
    def test_rhythm_pattern_becomes_the_realization(self):
        rp = {"defaultPattern": [1, -1, 2]}
        for extra in ({}, {"realizationStrategy": "block"}):
            doc = template(dict({"rhythmPattern": rp}, **extra))
            out, _, log, _ = convert(doc)
            self.assertEqual(passage_of(out), {"realization": {"name": "rhythm_pattern", "settings": rp}})
            self.assertEqual(len(log), 1 if extra else 0)

    def test_realization_strategy_alone_is_dropped(self):
        out, _, log, _ = convert(template({"realizationStrategy": "rhythm_pattern", "phrases": []}))
        self.assertEqual(passage_of(out), {"phrases": []})
        self.assertIn("dropped realizationStrategy", log[0])


class ProfileSelectorTests(unittest.TestCase):
    def test_config_becomes_the_selectors_settings(self):
        cfg = {"priorityMin": 0.2}
        out, _, log, _ = convert(template({"voicingProfileSelector": "drift",
                                           "voicingProfileSelectorConfig": cfg}))
        self.assertEqual(passage_of(out), {"voicingProfileSelector": {"name": "drift", "settings": cfg}})
        self.assertEqual(log, [])

    def test_config_of_a_selector_without_settings_is_dropped(self):
        for doc in ({"voicingProfileSelector": "static", "voicingProfileSelectorConfig": {}},
                    {"voicingProfileSelectorConfig": {"sequence": []}}):
            out, _, log, _ = convert(template(doc))
            self.assertNotIn("voicingProfileSelectorConfig", passage_of(out))
            self.assertIn("dropped voicingProfileSelectorConfig", log[0])


class FormTests(unittest.TestCase):
    def test_text_outside_the_keys_is_kept(self):
        text = ('{\n  "parts": [ {"name": "m", "role": "melody",\n'
                '    "passages": {"Main": {"phrases":   [],\n'
                '       "strategy": "library_passage", "libraryConfig": {"seed": 1}  }}}],\n'
                '  "bpm":   100\n}\n')
        new, count = ct.convert_text(text, [])
        self.assertEqual(count, 1)
        self.assertEqual(new, text.replace(
            '"strategy": "library_passage", "libraryConfig": {"seed": 1}',
            '"strategy": {"name": "library_passage", "settings": {"seed": 1}}'))

    def test_converting_twice_changes_nothing(self):
        doc = template({"strategy": "pedal_buildup", "pedalBuildupConfig": {"levels": 3},
                        "rhythmPattern": {"defaultPattern": [4]},
                        "voicingProfileSelector": "random",
                        "voicingProfileSelectorConfig": {"priorityMax": 0.5}})
        _, _, _, once = convert(doc, indent=2)
        twice, count = ct.convert_text(once, [])
        self.assertEqual((twice, count), (once, 0))

    def test_a_file_that_is_not_json_is_left_alone(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "prose.json"
            p.write_text("not json at all")
            self.assertFalse(ct.convert_file(p))
            self.assertEqual(p.read_text(), "not json at all")


if __name__ == "__main__":
    unittest.main()

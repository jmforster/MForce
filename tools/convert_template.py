"""Rewrites a composition template's settings keys into strategy choices.

    python tools/convert_template.py <file or folder> [...]

A template names a strategy in one of two shapes, a bare name or a name with
the settings written for it:

    "strategy": "sequence_passage"
    "strategy": {"name": "pedal_buildup", "settings": {"levels": 3}}

This tool rewrites the keys of the form before that, and changes nothing else:

- a strategy's settings key (periodConfig, sentenceConfig, twoFigureConfig,
  elaboratedConfig on a phrase; libraryConfig, pedalBuildupConfig,
  sequenceConfig, connectiveConfig, wanderingConfig on a passage) becomes the
  settings of the strategy the object names;
- a passage's `periods` becomes period_passage's `settings.periods`;
- a passage's `rhythmPattern` becomes
  `"realization": {"name": "rhythm_pattern", "settings": <it>}`, whatever
  `realizationStrategy` says (the pattern is what plays); without a
  `rhythmPattern`, `realizationStrategy` is dropped (it was never read);
- `voicingProfileSelectorConfig` becomes the settings of the
  `voicingProfileSelector` the passage names;
- a settings key whose strategy is not the one the object names is dropped,
  since nothing read it. Each drop is printed.

The rewrite splices text, so a file keeps its formatting: only the converted
keys change. A converted file converts to itself. A folder is walked for
*.json; a file that is not JSON is skipped with a message.
"""
import json
import pathlib
import sys

PHRASE_KEYS = {
    "periodConfig": "period_phrase",
    "sentenceConfig": "sentence_phrase",
    "twoFigureConfig": "two_figure_phrase",
    "elaboratedConfig": "elaborated_phrase",
}
PASSAGE_KEYS = {
    "libraryConfig": "library_passage",
    "pedalBuildupConfig": "pedal_buildup",
    "sequenceConfig": "sequence_passage",
    "connectiveConfig": "connective_passage",
    "wanderingConfig": "wandering_passage",
    "periods": "period_passage",
}
PROFILE_SELECTORS_WITH_SETTINGS = {"random", "drift", "scripted"}


# --------------------------------------------------------------------------
# A JSON reader that keeps where each value and member sits in the text.
# --------------------------------------------------------------------------

class Node:
    def __init__(self, kind, start):
        self.kind = kind          # "object", "array", "string", "other"
        self.start = start
        self.end = None
        self.members = []         # object: [Member]
        self.items = []           # array: [Node]
        self.value = None         # string: its value

    def member(self, key):
        for m in self.members:
            if m.key == key:
                return m
        return None


class Member:
    def __init__(self, key, key_start, node):
        self.key = key
        self.key_start = key_start
        self.node = node


class Reader:
    def __init__(self, text):
        self.t = text
        self.i = 0

    def ws(self):
        while self.i < len(self.t) and self.t[self.i] in " \t\r\n":
            self.i += 1

    def value(self):
        self.ws()
        c = self.t[self.i]
        if c == "{":
            return self.obj()
        if c == "[":
            return self.arr()
        if c == '"':
            n = Node("string", self.i)
            n.value = self.string()
            n.end = self.i
            return n
        n = Node("other", self.i)
        while self.i < len(self.t) and self.t[self.i] not in ",]} \t\r\n":
            self.i += 1
        n.end = self.i
        return n

    def string(self):
        start = self.i
        self.i += 1
        while self.t[self.i] != '"':
            self.i += 2 if self.t[self.i] == "\\" else 1
        self.i += 1
        return json.loads(self.t[start:self.i])

    def obj(self):
        n = Node("object", self.i)
        self.i += 1
        self.ws()
        if self.t[self.i] == "}":
            self.i += 1
            n.end = self.i
            return n
        while True:
            self.ws()
            ks = self.i
            key = self.string()
            self.ws()
            assert self.t[self.i] == ":", self.i
            self.i += 1
            n.members.append(Member(key, ks, self.value()))
            self.ws()
            if self.t[self.i] == ",":
                self.i += 1
                continue
            assert self.t[self.i] == "}", self.i
            self.i += 1
            n.end = self.i
            return n

    def arr(self):
        n = Node("array", self.i)
        self.i += 1
        self.ws()
        if self.t[self.i] == "]":
            self.i += 1
            n.end = self.i
            return n
        while True:
            n.items.append(self.value())
            self.ws()
            if self.t[self.i] == ",":
                self.i += 1
                continue
            assert self.t[self.i] == "]", self.i
            self.i += 1
            n.end = self.i
            return n


# --------------------------------------------------------------------------
# Finding the first old key, innermost first, and the edit that converts it.
# --------------------------------------------------------------------------

def strategy_name(obj):
    m = obj.member("strategy")
    if m is None:
        return ""
    if m.node.kind == "string":
        return m.node.value
    if m.node.kind == "object":
        name = m.node.member("name")
        return name.node.value if name is not None and name.node.kind == "string" else ""
    return ""


def deletion(text, obj, member):
    """The (start, end) span to delete for one member, comma included."""
    idx = obj.members.index(member)
    if len(obj.members) == 1:
        return member.key_start, member.node.end
    if idx > 0:
        return obj.members[idx - 1].node.end, member.node.end
    return member.key_start, obj.members[1].key_start


def wrap(name, settings_text):
    return '{"name": "%s", "settings": %s}' % (name, settings_text)


def convert_choice(text, obj, old_key, strategy, choice_key, path, log):
    """Edits that move `old_key`'s value into the choice under `choice_key`
    when that choice names `strategy`; else drops it."""
    m = obj.member(old_key)
    value = text[m.node.start:m.node.end]
    if old_key == "periods":
        value = '{"periods": %s}' % value
    choice = obj.member(choice_key)
    named = choice is not None and (
        (choice.node.kind == "string" and choice.node.value == strategy) or
        (choice.node.kind == "object" and strategy_name_of(choice.node) == strategy
         and choice.node.member("settings") is None))
    if not named:
        log.append("dropped %s at %s: %s is %r, not %r"
                   % (old_key, path, choice_key, choice_text(choice), strategy))
        return [(deletion(text, obj, m), "")]
    return [((choice.node.start, choice.node.end), wrap(strategy, value)),
            (deletion(text, obj, m), "")]


def strategy_name_of(node):
    name = node.member("name")
    return name.node.value if name is not None and name.node.kind == "string" else ""


def choice_text(choice):
    if choice is None:
        return ""
    if choice.node.kind == "string":
        return choice.node.value
    return strategy_name_of(choice.node)


def passage_edits(text, obj, path, log):
    for key, strategy in PASSAGE_KEYS.items():
        if obj.member(key) is not None:
            return convert_choice(text, obj, key, strategy, "strategy", path, log)
    rp = obj.member("rhythmPattern")
    rs = obj.member("realizationStrategy")
    if rp is not None:
        edits = [((rp.key_start, rp.node.end),
                  '"realization": ' + wrap("rhythm_pattern", text[rp.node.start:rp.node.end]))]
        if rs is not None:
            log.append("replaced realizationStrategy %r at %s by the rhythm pattern's "
                       "realization" % (choice_text(rs), path))
            edits.append((deletion(text, obj, rs), ""))
        return edits
    if rs is not None:
        log.append("dropped realizationStrategy %r at %s: no rhythmPattern, so it was "
                   "never read" % (choice_text(rs), path))
        return [(deletion(text, obj, rs), "")]
    cfg = obj.member("voicingProfileSelectorConfig")
    if cfg is not None:
        sel = obj.member("voicingProfileSelector")
        name = choice_text(sel)
        if sel is not None and sel.node.kind == "string" and name in PROFILE_SELECTORS_WITH_SETTINGS:
            return [((sel.node.start, sel.node.end), wrap(name, text[cfg.node.start:cfg.node.end])),
                    (deletion(text, obj, cfg), "")]
        log.append("dropped voicingProfileSelectorConfig at %s: voicingProfileSelector is "
                   "%r, which reads no settings" % (path, name))
        return [(deletion(text, obj, cfg), "")]
    return None


def phrase_edits(text, obj, path, log):
    for key, strategy in PHRASE_KEYS.items():
        if obj.member(key) is not None:
            return convert_choice(text, obj, key, strategy, "strategy", path, log)
    return None


def first_edit(text, node, role, path, log):
    """Post-order: an object's children before the object itself."""
    if node.kind == "object":
        for m in node.members:
            child_role = None
            if role == "passages":
                child_role = "passage"
            elif m.key == "passages":
                child_role = "passages"
            elif m.key in ("antecedent", "consequent"):
                child_role = "phrase"
            elif m.key == "phrases":
                child_role = "phrases"
            found = first_edit(text, m.node, child_role, path + "." + m.key, log)
            if found:
                return found
        if role == "passage":
            return passage_edits(text, node, path, log)
        if role == "phrase":
            return phrase_edits(text, node, path, log)
    elif node.kind == "array":
        for k, item in enumerate(node.items):
            found = first_edit(text, item, "phrase" if role == "phrases" else None,
                               "%s[%d]" % (path, k), log)
            if found:
                return found
    return None


def convert_text(text, log):
    """The converted text and the number of edits made."""
    count = 0
    while True:
        root = Reader(text).value()
        edits = first_edit(text, root, None, "$", log)
        if not edits:
            return text, count
        for (a, b), new in sorted(edits, key=lambda e: e[0][0], reverse=True):
            text = text[:a] + new + text[b:]
        count += 1


def convert_file(path):
    raw = path.read_bytes()
    text = raw.decode("utf-8")
    try:
        json.loads(text)
    except ValueError:
        print("%s: not JSON, skipped" % path)
        return False
    log = []
    new, count = convert_text(text, log)
    for line in log:
        print("%s: %s" % (path, line))
    if count == 0:
        return False
    json.loads(new)   # the rewrite is still JSON
    path.write_bytes(new.encode("utf-8"))
    print("%s: converted (%d edits)" % (path, count))
    return True


def main(args):
    if not args:
        print(__doc__)
        return 2
    for a in args:
        p = pathlib.Path(a)
        files = sorted(p.rglob("*.json")) if p.is_dir() else [p]
        for f in files:
            convert_file(f)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

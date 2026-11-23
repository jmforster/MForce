"""A small reader of C++ text, for the meter's rules that need to know where
they are: the code with its comments, string literals and preprocessor lines
blanked out, the comments by line, and the statements made at namespace scope.

It is a reader, not a parser: it knows braces, parentheses, comments, string
and character literals (raw strings and digit separators included) and the
statement boundaries `;`, `{` and `}`. What it cannot see is written down
where it decides (see namespace_statements)."""
import re

_IDENT = re.compile(r"[A-Za-z_0-9]")
_RAW_PREFIXES = ("R", "u8R", "uR", "UR", "LR")


def _raw_string_start(text, i):
    """Whether the quote at i opens a raw string (R"delim( ... )delim")."""
    j = i
    while j > 0 and _IDENT.match(text[j - 1]):
        j -= 1
    return text[j:i] in _RAW_PREFIXES


def _digit_separator(text, i):
    """Whether the apostrophe at i separates digits (1'000) rather than
    opening a character literal."""
    j = i
    while j > 0 and re.match(r"[0-9A-Fa-fxXbB.']", text[j - 1]):
        j -= 1
    if j == i or not text[j].isdigit():
        return False
    return j == 0 or not _IDENT.match(text[j - 1])


def _end_of_quoted(text, i, quote):
    """The index just past the literal opened by the quote at i."""
    j = i + 1
    while j < len(text) and text[j] != quote and text[j] != "\n":
        j += 2 if text[j] == "\\" else 1
    return min(j + 1, len(text))


def _end_of_raw(text, i):
    open_paren = text.find("(", i)
    delim = text[i + 1:open_paren]
    close = text.find(")" + delim + '"', open_paren)
    return len(text) if close < 0 else close + len(delim) + 2


def lex(text):
    """(code, comments): the text with every comment, literal and
    preprocessor line replaced by blanks (newlines kept, so line numbers hold;
    a literal leaves its quotes), and [(line, comment text)] with one entry
    per line a comment covers."""
    text = text.replace("\r\n", "\n")
    code, comments = [], []
    i, line, line_start, directive = 0, 1, True, False
    while i < len(text):
        c = text[i]
        if text.startswith("//", i) or text.startswith("/*", i):
            if text[i + 1] == "/":
                end = text.find("\n", i)
                end = len(text) if end < 0 else end
            else:
                end = text.find("*/", i + 2)
                end = len(text) if end < 0 else end + 2
            for k, part in enumerate(text[i:end].split("\n")):
                comments.append((line + k, part))
            newlines = text.count("\n", i, end)
            code.append(" " + "\n" * newlines)
            line += newlines
            i = end
            continue
        if c == '"' or (c == "'" and not _digit_separator(text, i)):
            end = _end_of_raw(text, i) if c == '"' and _raw_string_start(text, i) else _end_of_quoted(text, i, c)
            newlines = text.count("\n", i, end)
            code.append((" " if directive else c + c) + "\n" * newlines)
            line += newlines
            i = end
            continue
        if c == "#" and line_start:
            directive = True
        if c == "\n":
            line += 1
            directive = directive and i > 0 and text[i - 1] == "\\"
            line_start = True
        elif not c.isspace():
            line_start = False
        code.append(c if c == "\n" or not directive else " ")
        i += 1
    return "".join(code), comments


_NAMESPACE_HEAD = re.compile(r'^(?:inline\s+)?namespace\b[^=]*$|^extern\s*""\s*$')
_TYPE_HEAD = re.compile(r"^(?:class|struct|union|enum)\b")


def _depth0(s):
    """The text of s outside every (), [] and {} pair."""
    out, depth = [], 0
    for ch in s:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth = max(depth - 1, 0)
        elif depth == 0:
            out.append(ch)
    return "".join(out)


def has_assignment(head):
    """An `=` outside brackets that is not part of an operator name or a comparison."""
    if re.search(r"\boperator\b", head):
        return False
    return re.search(r"(?<![=<>!])=(?!=)", _depth0(head)) is not None


def _brace_kind(head):
    """What a `{` opened at namespace scope after `head` belongs to: "ns" (a
    namespace or extern "C" block), "fn" (a function body: the statement ends
    at its `}`) or "init" (a type body or a braced initialiser: the statement
    goes on after its `}`)."""
    h = head.strip()
    if _NAMESPACE_HEAD.match(h):
        return "ns"
    if _TYPE_HEAD.match(h) or has_assignment(h):
        return "init"
    if h.startswith("template") and _TYPE_HEAD.match(re.sub(r"^template\s*<.*?>\s*", "", h, flags=re.S)):
        return "init"
    return "fn" if "(" in h else "init"


def namespace_statements(code):
    """[(line, text)] for every statement made at namespace scope, where every
    open brace belongs to a namespace (named, anonymous or extern "C"). A
    braced body inside a statement is kept as `{}`. A function definition
    ends at its body's `}` and is not listed. Not seen: a `#if` whose branches
    open braces differently is read as written, both branches at once."""
    out, stack, head, start = [], [], [], None
    line = 1
    for ch in code:
        if ch == "\n":
            line += 1
        if stack and stack[-1][0] != "ns":
            if ch == "{":
                stack.append(("inner", None))
            elif ch == "}":
                kind, saved = stack.pop()
                if kind == "fn":
                    head, start = [], None
                elif kind == "init":
                    head, start = list(saved[0] + "{}"), saved[1]
            continue
        if ch == "{":
            text = "".join(head)
            kind = _brace_kind(text)
            stack.append((kind, (text, start)))
            if kind == "ns":
                head, start = [], None
        elif ch == "}":
            if stack:
                stack.pop()
            head, start = [], None
        elif ch == ";":
            out.append((start or line, "".join(head)))
            head, start = [], None
        else:
            if start is None and not ch.isspace():
                start = line
            head.append(ch)
    return [(ln, " ".join(t.split())) for ln, t in out if t.strip()]

"""Turn source text into numbers. Pure functions over text, plus two that
call lizard (function lengths read a file; token duplicates run lizard as
a subprocess). Every regex here is the definition of the number it feeds."""
import functools
import re
import subprocess
import sys

import lizard

import cxx

_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
# What the changelog rule can be certain of in a comment: a date (2026-11-23,
# 2026/11/23) or a tracker id (backlog, CLEANUP or REVIEW and a number, with or
# without #). History in words is a reviewer's to find, not this pattern's.
_CHANGELOG = re.compile(r'\b20\d\d[-/]\d\d[-/]\d\d\b|\b(?:backlog|BACKLOG|CLEANUP|REVIEW)\s*#?\s*\d')
# The statements that are never a variable, by their first word.
_NOT_A_VARIABLE = re.compile(r'^(?:using|typedef|static_assert|template|namespace|extern|friend)\b')
_TYPE_HEAD = re.compile(r'^(?:class|struct|union|enum)\b')
_DECORATION = re.compile(r'\[\[.*?\]\]|\b(?:alignas|__declspec)\s*\([^()]*\)')
_TYPE_DECL = re.compile(r'^(?:struct|class|enum class|enum)\s+([A-Za-z_]\w*)\b(?!\s*;)')
_DUP_SPAN = re.compile(r'^(.*):(\d+) ~ (\d+)$')


def lines_of(text):
    """The file's lines, counted as `wc -l` counts them: a trailing newline
    ends the last line rather than starting an empty one."""
    text = text.replace("\r\n", "\n")
    if text.endswith("\n"):
        text = text[:-1]
    return text.split("\n")


def includes(text):
    """Every #include target, in order."""
    return [m.group(1) for m in map(_INCLUDE.match, lines_of(text)) if m]


@functools.lru_cache(maxsize=4)
def _lexed(text):
    return cxx.lex(text)


def _specifiers(stmt):
    """The declaration's text before its initialiser, with template arguments
    and parenthesised groups taken out: where `const` would say the variable
    is const."""
    spec = re.split(r'=|\{', stmt, maxsplit=1)[0]
    while True:
        shorter = re.sub(r'<[^<>]*>', ' ', re.sub(r'\([^()]*\)', ' ', spec))
        if shorter == spec:
            return spec
        spec = shorter


def _is_mutable_variable(stmt):
    """Whether a namespace-scope statement defines a variable that is not
    const: not const, constexpr or constinit const (a pointer to const reads
    as const), not extern, and not a function, type, alias, template, using,
    static_assert or macro call. A variable initialised with parentheses
    (`Foo x(1);`) reads as a function declaration and is not counted."""
    s = _DECORATION.sub(" ", stmt).strip()
    if not s or _NOT_A_VARIABLE.match(s):
        return False
    if _TYPE_HEAD.match(s):
        rest = s.rsplit("{}", 1)[1] if "{}" in s else ""
        if not re.match(r'\s*[A-Za-z_*&]', rest):
            return False
    elif "(" in s and "{}" not in s and not cxx.has_assignment(s):
        return False
    return not re.search(r'\b(?:const|constexpr)\b', _specifiers(s))


def mutable_variables(text):
    """[(line, statement)]: the variables defined at namespace scope (every
    open brace a namespace's) that are not const. `thread_local` and `inline`
    variables count."""
    code, _ = _lexed(text)
    return [(ln, s) for ln, s in cxx.namespace_statements(code) if _is_mutable_variable(s)]


def file_level_mutable_vars(text):
    """The number of mutable_variables."""
    return len(mutable_variables(text))


def changelog_lines(text):
    """The lines whose comment holds a date or a tracker id."""
    _, comments = _lexed(text)
    return sorted({ln for ln, c in comments if _CHANGELOG.search(c)})


def flag_counts(text):
    """Counts of constructs worth watching."""
    ls = lines_of(text)
    return {
        "const_cast": sum("const_cast" in l for l in ls),
        "dynamic_cast": sum("dynamic_cast" in l for l in ls),
        "changelog_comments": len(changelog_lines(text)),
    }


def type_declarations(text):
    """Names of struct/class/enum declared at column 0 (not forward
    declarations). Nested types are not listed."""
    return [m.group(1) for m in map(_TYPE_DECL.match, lines_of(text)) if m]


def function_lengths(path):
    """[(name, start_line, length)] via lizard."""
    return [(name, start, length) for name, _, start, length in functions(path)]


def functions(path):
    """[(name, signature, start_line, length)] via lizard. The signature is
    lizard's long name: the qualified name with its parameter list."""
    info = lizard.analyze_file(path)
    return [(f.name, f.long_name, f.start_line, f.length) for f in info.function_list]


def parse_duplicates(lizard_output):
    """Parse lizard's -Eduplicate text into
    [(span_lines, [(path, start, end), ...])], largest first."""
    blocks, cur = [], None
    for line in lizard_output.split("\n"):
        if line.startswith("Duplicate block:"):
            cur = []
            blocks.append(cur)
        elif cur is not None and line.startswith("^^^"):
            cur = None
        elif cur is not None:
            m = _DUP_SPAN.match(line.strip())
            if m:
                cur.append((m.group(1).replace("\\", "/"), int(m.group(2)), int(m.group(3))))
    result = [(b[0][2] - b[0][1] + 1, b) for b in blocks if b]
    result.sort(key=lambda x: (-x[0], x[1]))
    return result


def token_duplicates(paths, root):
    """Duplicate blocks found by lizard's token-based detector over `paths`
    (relative to `root`). Token comparison catches copies that differ only
    by renamed variables, which a line comparison misses."""
    out = subprocess.run([sys.executable, "-m", "lizard", "-Eduplicate"] + list(paths),
                         cwd=root, capture_output=True, text=True).stdout
    return parse_duplicates(out)
